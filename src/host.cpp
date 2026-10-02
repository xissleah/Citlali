#include "host.h"
#include "frontend.h"
#include "text.h"
#include "probe.h"
#include "plugin_support.h"
#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <set>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace citlali {
CitlaliStringView view(const std::string &s) {
    return cp::view(s);
}
CitlaliStringView view(const char *s) {
    return cp::view(s);
}
std::string string(CitlaliStringView s) {
    return cp::string(s);
}
CitlaliErrorV1 error_slot() {
    return cp::err();
}
void require_status(CitlaliStatus s, CitlaliErrorV1 &e, const std::string &w) {
    try {
        cp::check(s, e);
    } catch (const std::exception &x) {
        throw std::runtime_error(w + ": " + x.what());
    }
}
struct Host::Node {
    Host *host;
    std::string ref, package, deployment;
    Manifest *manifest;
    Selection *selection;
    std::unique_ptr<Library> library;
    const CitlaliPluginApiV1 *api = nullptr;
    CitlaliInstance instance = nullptr;
    CitlaliHostV1 context{};
    bool started = false;
};
void CITLALI_CALL Host::log(void *ctx, uint32_t, CitlaliStringView msg) noexcept {
    try {
        auto n = static_cast<Node *>(ctx);
        std::lock_guard<std::mutex> lock(n->host->log_mutex_);
        std::cerr << "[" << n->ref << "] " << string(msg) << "\n";
    } catch (...) {
    }
}
uint64_t CITLALI_CALL Host::count(void *ctx, CitlaliStringView slot) noexcept {
    try {
        auto n = static_cast<Node *>(ctx);
        auto b = n->selection->bindings.find(string(slot));
        return b == n->selection->bindings.end() ? 0 : b->second.size();
    } catch (...) {
        return 0;
    }
}
CitlaliStatus CITLALI_CALL Host::dependency(void *ctx, CitlaliStringView slot, uint64_t index,
                                            CitlaliInstance *out, CitlaliStringView *identity,
                                            CitlaliErrorV1 *e) noexcept {
    if (out)
        *out = nullptr;
    return cp::guard(e, [&] {
        cp::need(out && identity, "null dependency output");
        auto n = static_cast<Node *>(ctx);
        auto b = n->selection->bindings.find(string(slot));
        cp::need(b != n->selection->bindings.end() && index < b->second.size(),
                 "dependency outside bound slot", CITLALI_MISSING_DEPENDENCY);
        auto target = n->host->lookup_.at(b->second[index]);
        cp::need(target->instance != nullptr, "dependency not yet created", CITLALI_MISSING_DEPENDENCY);
        *out = target->instance;
        *identity = view(target->ref);
    });
}
CitlaliStatus CITLALI_CALL Host::query_dependency(void *ctx, CitlaliStringView slot, uint64_t index,
                                                  CitlaliStringView protocol, const CitlaliInterfaceV1 **out,
                                                  CitlaliErrorV1 *e) noexcept {
    if (out)
        *out = nullptr;
    return cp::guard(e, [&] {
        cp::need(out, "null table output");
        auto n = static_cast<Node *>(ctx);
        auto b = n->selection->bindings.find(string(slot));
        cp::need(b != n->selection->bindings.end() && index < b->second.size(),
                 "dependency outside bound slot", CITLALI_MISSING_DEPENDENCY);
        *out = n->host->query_interface(b->second[index], string(protocol));
    });
}
Host::Host(Registry r, bool strict_env, bool create) : registry_(std::move(r)) {
    try {
        load(strict_env);
        if (create)
            for (auto &n : nodes_) {
                auto e = error_slot();
                auto status =
                    n->api->create_instance(&n->context, view(n->selection->config), &n->instance, &e);
                require_status(status, e, n->ref + " create");
                if (!n->instance)
                    throw std::runtime_error(n->ref + ": create returned null instance");
                log(n.get(), 1, view("created"));
            }
    } catch (...) {
        auto original = std::current_exception();
        try {
            close();
        } catch (const std::exception &x) {
            std::cerr << "cleanup failed after startup: " << x.what() << "; exiting without unsafe unload\n";
            std::_Exit(2);
        }
        std::rethrow_exception(original);
    }
}
void Host::load(bool strict_env) {
    for (auto &ref : registry_.order) {
        auto node = std::make_unique<Node>();
        node->host = this;
        node->ref = ref;
        node->manifest = &registry_.manifests.at(ref);
        node->selection = &registry_.selected.at(ref);
        node->package = node->manifest->path.u8string();
        node->deployment = registry_.deployment.path.parent_path().u8string();
        auto n = node.get();
        nodes_.push_back(std::move(node));
        lookup_[ref] = n;
        n->library = std::make_unique<Library>(n->manifest->entry);
        auto entry = reinterpret_cast<CitlaliEntryV1>(n->library->symbol(n->manifest->symbol));
        auto e = error_slot();
        require_status(entry(CITLALI_NATIVE_ABI, &n->api, &e), e, ref + " entry");
        auto a = n->api;
        if (!a || a->struct_size < sizeof(*a) || a->native_abi != CITLALI_NATIVE_ABI || !a->query_interface ||
            !a->create_instance || !a->destroy_instance)
            throw std::runtime_error(ref + ": incompatible API table");
        if (string(a->id) != n->manifest->id || string(a->version) != n->manifest->version ||
            string(a->type) != n->manifest->type)
            throw std::runtime_error(ref + ": manifest identity mismatch");
        if (a->protocol_count > 1024 || (a->protocol_count && !a->protocols))
            throw std::runtime_error(ref + ": invalid protocol array");
        std::set<std::string> actual;
        for (uint64_t i = 0; i < a->protocol_count; ++i)
            if (!actual.insert(string(a->protocols[i])).second)
                throw std::runtime_error(ref + ": duplicate actual protocol");
        if (actual != std::set<std::string>(n->manifest->protocols.begin(), n->manifest->protocols.end()))
            throw std::runtime_error(ref + ": manifest capability mismatch");
        for (auto &p : n->manifest->protocols)
            query(ref, p);
        n->context = {sizeof(CitlaliHostV1), 1, n, log, count, dependency, query_dependency, view(n->package),
                      view(n->deployment)};
        bool failed = false, unknown = false;
        for (auto &p : n->manifest->probes) {
            uint32_t result = CITLALI_ENV_UNKNOWN;
            if (p.checker == "self" && actual.count(CITLALI_PROBE_PROTOCOL)) {
                auto table = static_cast<const CitlaliProbeV1 *>(query(ref, CITLALI_PROBE_PROTOCOL));
                if (table->struct_size < sizeof(*table) || !table->evaluate)
                    throw std::runtime_error(ref + ": invalid probe table");
                e = error_slot();
                require_status(table->evaluate(view(p.id), view(p.op), view(p.value), &result, &e), e,
                               ref + " probe " + p.id);
                if (result < 1 || result > 3)
                    throw std::runtime_error(ref + ": invalid probe state");
            }
            if (result == CITLALI_ENV_FAIL)
                failed = true;
            else if (result == CITLALI_ENV_UNKNOWN)
                unknown = true;
        }
        if (failed)
            throw std::runtime_error(ref + ": dynamic environment FAIL");
        if (unknown) {
            if (strict_env)
                throw std::runtime_error(ref + ": dynamic environment UNKNOWN (--strict-env)");
            std::cerr << "warning: " << ref << ": dynamic environment UNKNOWN; trying initialization\n";
        }
    }
}
const void *Host::query(const std::string &ref, const std::string &protocol) const {
    return query_interface(ref, protocol)->functions;
}
const CitlaliInterfaceV1 *Host::query_interface(const std::string &ref, const std::string &protocol) const {
    auto n = lookup_.at(ref);
    if (std::find(n->manifest->protocols.begin(), n->manifest->protocols.end(), protocol) ==
        n->manifest->protocols.end())
        throw std::runtime_error(ref + ": unsupported protocol " + protocol);
    const CitlaliInterfaceV1 *table = nullptr;
    auto e = error_slot();
    require_status(n->api->query_interface(view(protocol), &table, &e), e, ref + " query " + protocol);
    cp::functions(table, protocol);
    return table;
}
CitlaliInstance Host::instance(const std::string &ref) const {
    return lookup_.at(ref)->instance;
}
Host::~Host() {
    try {
        close();
    } catch (const std::exception &x) {
        std::cerr << "shutdown failed: " << x.what() << "; exiting without unsafe unload\n";
        std::_Exit(2);
    }
}
void Host::close() {
    if (closed_)
        return;
    // All stop calls precede destruction; frontend destroy/join and runtime destroy wait for callbacks.
    for (auto &n : nodes_)
        if (n->instance && n->started) {
            auto f = static_cast<const CitlaliFrontendV1 *>(query(n->ref, CITLALI_FRONTEND_PROTOCOL));
            auto e = error_slot();
            require_status(f->request_stop(n->instance, &e), e, n->ref + " stop frontend");
        }
    for (auto &n : nodes_)
        if (n->instance && n->manifest->type == "runtime" &&
            std::find(n->manifest->protocols.begin(), n->manifest->protocols.end(), CITLALI_TEXT_PROTOCOL) !=
                n->manifest->protocols.end()) {
            auto t = static_cast<const CitlaliTextV1 *>(query(n->ref, CITLALI_TEXT_PROTOCOL));
            auto e = error_slot();
            if (t->struct_size < sizeof(*t) || !t->request_stop)
                throw std::runtime_error("invalid text stop table");
            require_status(t->request_stop(n->instance, &e), e, n->ref + " stop runtime");
        }
    for (auto i = nodes_.rbegin(); i != nodes_.rend(); ++i) {
        auto &n = *i;
        if (n->instance) {
            auto e = error_slot();
            require_status(n->api->destroy_instance(n->instance, &e), e, n->ref + " destroy");
            n->instance = nullptr;
            log(n.get(), 1, view("destroyed"));
        }
    }
    for (auto i = nodes_.rbegin(); i != nodes_.rend(); ++i) {
        (*i)->api = nullptr;
        (*i)->library.reset();
    }
    lookup_.clear();
    nodes_.clear();
    closed_ = true;
}
namespace {
volatile std::sig_atomic_t stopped = 0;
#ifdef _WIN32
volatile LONG console_stopped = 0;
BOOL WINAPI console_stop(DWORD event) {
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT)
        return FALSE;
    InterlockedExchange(&console_stopped, 1);
    return TRUE;
}
#endif
void signal_stop(int) {
    stopped = 1;
}
} // namespace
void Host::run() {
    stopped = 0;
    auto oldint = std::signal(SIGINT, signal_stop);
    auto oldterm = std::signal(SIGTERM, signal_stop);
#ifdef _WIN32
    InterlockedExchange(&console_stopped, 0);
    // Parent launchers can pass an inherited ignore-Ctrl+C flag.
    SetConsoleCtrlHandler(nullptr, FALSE);
    const bool console_handler = SetConsoleCtrlHandler(console_stop, TRUE) != 0;
#endif
    try {
        std::vector<Node *> active;
        for (auto &n : nodes_)
            if (n->manifest->type == "cli" || n->manifest->type == "server") {
                auto f = static_cast<const CitlaliFrontendV1 *>(query(n->ref, CITLALI_FRONTEND_PROTOCOL));
                if (f->struct_size < sizeof(*f) || !f->start || !f->request_stop || !f->join)
                    throw std::runtime_error(n->ref + ": invalid frontend table");
                auto e = error_slot();
                require_status(f->start(n->instance, &e), e, n->ref + " start");
                n->started = true;
                active.push_back(n.get());
            }
        while (!active.empty() && !stopped) {
#ifdef _WIN32
            if (InterlockedCompareExchange(&console_stopped, 0, 0))
                break;
#endif
            for (auto i = active.begin(); i != active.end();) {
                auto n = *i;
                auto f = static_cast<const CitlaliFrontendV1 *>(query(n->ref, CITLALI_FRONTEND_PROTOCOL));
                auto e = error_slot();
                auto status = f->join(n->instance, 25, &e);
                if (status == CITLALI_TIMEOUT) {
                    if (e.release)
                        e.release(e.owner);
                    ++i;
                } else {
                    require_status(status, e, n->ref + " join");
                    i = active.erase(i);
                }
            }
        }
        close();
#ifdef _WIN32
        if (console_handler)
            SetConsoleCtrlHandler(console_stop, FALSE);
#endif
        std::signal(SIGINT, oldint);
        std::signal(SIGTERM, oldterm);
    } catch (...) {
#ifdef _WIN32
        if (console_handler)
            SetConsoleCtrlHandler(console_stop, FALSE);
#endif
        std::signal(SIGINT, oldint);
        std::signal(SIGTERM, oldterm);
        throw;
    }
}
} // namespace citlali
