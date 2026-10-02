#include "../protocol.h"
#include "input.h"
#include "plugin_support.h"
#include "toml.hpp"
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
namespace {
struct Frontend {
    CitlaliHostV1 host;
    CitlaliInstance runtime = nullptr;
    const CitlaliTextV1 *text = nullptr;
    CitlaliSession session = nullptr;
    CitlaliOperation operation = nullptr;
    std::string prompt, failure;
    bool one_shot = false;
    uint32_t limit = 16;
    std::atomic<bool> stopping{false};
    std::mutex mutex, output_mutex;
    std::condition_variable cv;
    bool done = false, started = false;
    std::thread thread;
    ~Frontend() {
        stopping = true;
        if (thread.joinable())
            thread.join();
    }
};
uint32_t CITLALI_CALL sink(void *ctx, const CitlaliTextEventV1 *event) noexcept {
    try {
        auto f = static_cast<Frontend *>(ctx);
        std::lock_guard<std::mutex> lock(f->output_mutex);
        if (event->kind == CITLALI_EVENT_TEXT)
            std::cout << cp::string(event->text) << std::flush;
        else if (event->kind == CITLALI_EVENT_CANCELLED)
            std::cout << "[cancelled]\n";
        else if (event->kind == CITLALI_EVENT_FAILED)
            std::cerr << "[task failed] " << cp::string(event->text) << "\n";
        return f->stopping ? 1 : 0;
    } catch (...) {
        return 1;
    }
}
void finish(Frontend &f, bool cancel) {
    if (!f.operation)
        return;
    auto e = cp::err();
    if (cancel)
        cp::check(f.text->cancel(f.runtime, f.operation, &e), e);
    uint32_t state = 0;
    // Poll completion so frontend shutdown can interrupt a synchronous UI turn.
    while (true) {
        if (f.stopping && !cancel) {
            cp::check(f.text->cancel(f.runtime, f.operation, &e), e);
            cancel = true;
        }
        auto status = f.text->wait(f.runtime, f.operation, 25, &state, &e);
        if (status != CITLALI_TIMEOUT) {
            cp::check(status, e);
            break;
        }
        if (e.release)
            e.release(e.owner);
        e = cp::err();
    }
    cp::check(f.text->release_operation(f.runtime, f.operation, &e), e);
    f.operation = nullptr;
    if (state == CITLALI_OPERATION_FAILED)
        throw cp::Failure(CITLALI_INTERNAL_ERROR, "runtime task failed");
}
void submit(Frontend &f, const std::string &prompt) {
    if (f.operation)
        finish(f, false);
    CitlaliTextRequestV1 request{sizeof(request), f.limit, cp::view(prompt)};
    auto e = cp::err();
    cp::check(f.text->submit(f.runtime, f.session, &request, sink, &f, &f.operation, &e), e);
}
void loop(Frontend *f) noexcept {
    try {
#ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);
#endif
        auto e = cp::err();
        cp::check(f->text->create_session(f->runtime, &f->session, &e), e);
        CitlaliStringView summary{};
        cp::check(f->text->plan_summary(f->runtime, &summary, &e), e);
        std::cout << cp::string(summary) << "\n";
        if (f->one_shot) {
            submit(*f, f->prompt);
            finish(*f, false);
        } else {
            std::cout << "Citlali CLI: Enter to generate; Ctrl+C to exit. :plan / :quit\n";
            cli::Input input;
            std::cout << "> " << std::flush;
            while (!f->stopping) {
                std::string line;
                auto result = input.poll(line);
                if (result == 2) {
                    finish(*f, false);
                    break;
                }
                if (result == 0)
                    continue;
                if (line == ":quit")
                    break;
                else if (line == ":plan")
                    std::cout << cp::string(summary) << "\n";
                else if (!line.empty()) {
                    submit(*f, line);
                    finish(*f, false);
                }
                if (!f->stopping)
                    std::cout << "> " << std::flush;
            }
        }
    } catch (const std::exception &x) {
        f->failure = x.what();
    } catch (...) {
        f->failure = "frontend worker exception";
    }
    try {
        finish(*f, true);
        if (f->session) {
            auto e = cp::err();
            cp::check(f->text->destroy_session(f->runtime, f->session, &e), e);
            f->session = nullptr;
        }
    } catch (const std::exception &x) {
        if (f->failure.empty())
            f->failure = x.what();
    }
    {
        std::lock_guard<std::mutex> lock(f->mutex);
        f->done = true;
    }
    f->cv.notify_all();
}
CitlaliStatus CITLALI_CALL create(const CitlaliHostV1 *h, CitlaliStringView config, CitlaliInstance *out,
                                  CitlaliErrorV1 *e) {
    if (out)
        *out = nullptr;
    return cp::guard(e, [&] {
        cp::need(h && out && h->struct_size >= sizeof(*h) && h->version == 1, "invalid host context");
        auto c = toml::parse(cp::string(config));
        for (auto &&[k, v] : c)
            cp::need(k.str() == "prompt" || k.str() == "max_tokens", "unknown CLI config field");
        auto f = std::make_unique<Frontend>();
        f->host = *h;
        f->text = cp::query<CitlaliTextV1>(*h, "runtime", 0, CITLALI_TEXT_PROTOCOL, &f->runtime);
        cp::need(f->text->create_session && f->text->destroy_session && f->text->submit && f->text->wait &&
                     f->text->cancel && f->text->release_operation && f->text->request_stop &&
                     f->text->plan_summary,
                 "incomplete text table", CITLALI_INCOMPATIBLE_ABI);
        if (c.contains("prompt")) {
            auto p = c["prompt"].value<std::string>();
            cp::need(p.has_value(), "prompt must be a string");
            f->prompt = *p;
            f->one_shot = true;
        }
        auto limit = c["max_tokens"].value_or<int64_t>(16);
        cp::need(!c.contains("max_tokens") || c["max_tokens"].is_integer(), "max_tokens must be integer");
        cp::need(limit > 0 && limit <= 4096, "max_tokens must be 1..4096");
        f->limit = static_cast<uint32_t>(limit);
        *out = reinterpret_cast<CitlaliInstance>(f.release());
    });
}
Frontend *frontend(CitlaliInstance h) {
    cp::need(h, "null frontend");
    return reinterpret_cast<Frontend *>(h);
}
CitlaliStatus CITLALI_CALL start(CitlaliInstance h, CitlaliErrorV1 *e) {
    return cp::guard(e, [&] {
        auto f = frontend(h);
        std::lock_guard<std::mutex> lock(f->mutex);
        cp::need(!f->started, "frontend already started", CITLALI_BUSY);
        f->started = true;
        try {
            f->thread = std::thread(loop, f);
        } catch (...) {
            f->started = false;
            throw;
        }
    });
}
CitlaliStatus CITLALI_CALL stop(CitlaliInstance h, CitlaliErrorV1 *e) {
    return cp::guard(e, [&] { frontend(h)->stopping = true; });
}
CitlaliStatus CITLALI_CALL join(CitlaliInstance h, uint64_t timeout, CitlaliErrorV1 *e) {
    return cp::guard(e, [&] {
        auto f = frontend(h);
        std::unique_lock<std::mutex> lock(f->mutex);
        cp::need(f->started, "frontend not started");
        if (timeout == UINT64_MAX)
            f->cv.wait(lock, [&] { return f->done; });
        else {
            cp::need(timeout <= 86400000, "timeout too large");
            if (!f->cv.wait_for(lock, std::chrono::milliseconds(timeout), [&] { return f->done; }))
                throw cp::Failure(CITLALI_TIMEOUT, "frontend still running");
        }
        if (f->thread.joinable())
            f->thread.join();
        cp::need(f->failure.empty(), f->failure, CITLALI_INTERNAL_ERROR);
    });
}
CitlaliStatus CITLALI_CALL destroy(CitlaliInstance h, CitlaliErrorV1 *e) {
    return cp::guard(e, [&] { delete frontend(h); });
}
CitlaliFrontendV1 table = {sizeof(table), 1, start, stop, join};
CitlaliStatus CITLALI_CALL query(CitlaliStringView id, const CitlaliInterfaceV1 **out, CitlaliErrorV1 *e) {
    if (out)
        *out = nullptr;
    return cp::guard(e, [&] {
        cp::need(out, "null interface output");
        if (cp::string(id) == CITLALI_FRONTEND_PROTOCOL) {
            static const auto descriptor = cp::interface(CITLALI_FRONTEND_PROTOCOL, &table);
            *out = &descriptor;
        }
        else
            throw cp::Failure(CITLALI_UNSUPPORTED, "unsupported frontend interface");
    });
}
CitlaliStringView protocols[] = {cp::view(CITLALI_FRONTEND_PROTOCOL)};
CitlaliPluginApiV1 api = {sizeof(api),
                          CITLALI_NATIVE_ABI,
                          cp::view("example.cli-basic"),
                          cp::view("0.1.0"),
                          cp::view("cli"),
                          protocols,
                          1,
                          query,
                          create,
                          destroy};
} // namespace
extern "C" CITLALI_EXPORT CitlaliStatus CITLALI_CALL citlali_plugin_entry(uint32_t v,
                                                                          const CitlaliPluginApiV1 **out,
                                                                          CitlaliErrorV1 *e) {
    if (out)
        *out = nullptr;
    return cp::guard(e, [&] {
        cp::need(out, "null API output");
        cp::need(v == CITLALI_NATIVE_ABI, "unsupported native ABI", CITLALI_INCOMPATIBLE_ABI);
        *out = &api;
    });
}
