#include "../protocol.h"
#include "plugin_support.h"
#include "toml.hpp"
#include <cstdlib>
#include <fstream>
namespace {
void trace(const std::string &s) {
    if (auto p = std::getenv("CITLALI_TEST_TRACE")) {
        std::ofstream f(p, std::ios::app);
        f << s << "\n";
    }
}
struct LibraryTrace {
    ~LibraryTrace() {
        trace("unload");
    }
} library_trace;
struct Instance {
    std::string name;
};
void CITLALI_CALL free_error(void *p) {
    trace("error.release");
    delete static_cast<std::string *>(p);
}
CitlaliStatus CITLALI_CALL create(const CitlaliHostV1 *host, CitlaliStringView config, CitlaliInstance *out,
                                  CitlaliErrorV1 *e) {
    *out = nullptr;
    auto status = cp::guard(e, [&] {
        auto c = toml::parse(cp::string(config));
        auto name = c["name"].value_or(std::string("fixture"));
        if (c["fail"].value_or(false))
            throw cp::Failure(CITLALI_INTERNAL_ERROR, "fixture create failure " + name);
        if (c["query_v2"].value_or(false)) {
            cp::need(host, "missing host");
            cp::query<CitlaliTableV1>(*host, "provider", 0, "test.lifecycle/events-v2",
                                      nullptr, nullptr, 2);
        }
        auto i = new Instance{name};
        trace("create " + name);
        *out = reinterpret_cast<CitlaliInstance>(i);
    });
    if (status != 0 && e->owner)
        e->release = free_error;
    return status;
}
CitlaliStatus CITLALI_CALL destroy(CitlaliInstance h, CitlaliErrorV1 *e) {
    return cp::guard(e, [&] {
        auto i = reinterpret_cast<Instance *>(h);
        trace("destroy " + i->name);
        delete i;
    });
}
CitlaliStatus CITLALI_CALL evaluate(CitlaliStringView id, CitlaliStringView op, CitlaliStringView value,
                                    uint32_t *out, CitlaliErrorV1 *e) {
    return cp::guard(e, [&] {
        cp::need(out, "null probe output");
        auto name = cp::string(id), o = cp::string(op), v = cp::string(value);
        *out = CITLALI_ENV_UNKNOWN;
        if (name != "demo.numeric")
            return;
        cp::need(!v.empty() && v.find_first_not_of("0123456789") == std::string::npos,
                 "invalid numeric probe value");
        auto n = std::stoull(v);
        bool ok = false;
        if (o == "eq")
            ok = 10 == n;
        else if (o == "ne")
            ok = 10 != n;
        else if (o == "gte")
            ok = 10 >= n;
        else if (o == "lte")
            ok = 10 <= n;
        else if (o == "gt")
            ok = 10 > n;
        else if (o == "lt")
            ok = 10 < n;
        else
            throw cp::Failure(CITLALI_INVALID_ARGUMENT, "invalid probe op");
        *out = ok ? CITLALI_ENV_PASS : CITLALI_ENV_FAIL;
    });
}
CitlaliProbeV1 probe = {sizeof(probe), 1, evaluate};
CitlaliTableV1 marker = {sizeof(marker), 1};
CitlaliTableV1 marker_v2 = {sizeof(marker_v2), 2};
uint32_t short_prefix = sizeof(uint32_t);
CitlaliStatus CITLALI_CALL query(CitlaliStringView id, const CitlaliInterfaceV1 **out, CitlaliErrorV1 *e) {
    *out = nullptr;
    return cp::guard(e, [&] {
        auto p = cp::string(id);
        if (p == CITLALI_PROBE_PROTOCOL) {
            static const auto descriptor = cp::interface(CITLALI_PROBE_PROTOCOL, &probe, 1);
            *out = &descriptor;
        }
        else if (p == "test.lifecycle/events-v1") {
            static const auto descriptor = cp::interface("test.lifecycle/events-v1", &marker, 1);
            *out = &descriptor;
        }
        else if (p == "test.lifecycle/events-v2") {
            static const auto descriptor = cp::interface("test.lifecycle/events-v2", &marker_v2, 2);
            *out = &descriptor;
        }
        else
            throw cp::Failure(CITLALI_UNSUPPORTED, "unsupported fixture interface");
    });
}
CitlaliStringView protocols[] = {cp::view("test.lifecycle/events-v1"), cp::view(CITLALI_PROBE_PROTOCOL),
                                 cp::view("test.lifecycle/events-v2")};
CitlaliPluginApiV1 api = {sizeof(api),
                          CITLALI_NATIVE_ABI,
                          cp::view("test.lifecycle"),
                          cp::view("0.1.0"),
                          cp::view("test.fixture"),
                          protocols,
                          3,
                          query,
                          create,
                          destroy};
} // namespace
extern "C" CITLALI_EXPORT CitlaliStatus CITLALI_CALL citlali_plugin_entry(uint32_t v,
                                                                          const CitlaliPluginApiV1 **out,
                                                                          CitlaliErrorV1 *e) {
    *out = nullptr;
    return cp::guard(e, [&] {
        trace("entry");
        cp::need(v == CITLALI_NATIVE_ABI, "wrong native ABI", CITLALI_INCOMPATIBLE_ABI);
        *out = &api;
    });
}
extern "C" CITLALI_EXPORT CitlaliStatus CITLALI_CALL incompatible_entry(uint32_t,
                                                                        const CitlaliPluginApiV1 **out,
                                                                        CitlaliErrorV1 *e) {
    *out = nullptr;
    return cp::error(e, CITLALI_INCOMPATIBLE_ABI, "incompatible fixture entry");
}
extern "C" CITLALI_EXPORT CitlaliStatus CITLALI_CALL mismatch_entry(uint32_t, const CitlaliPluginApiV1 **out,
                                                                    CitlaliErrorV1 *) {
    static auto wrong = api;
    wrong.id = cp::view("test.wrong");
    *out = &wrong;
    return 0;
}
extern "C" CITLALI_EXPORT CitlaliStatus CITLALI_CALL old_abi_entry(uint32_t, const CitlaliPluginApiV1 **out,
                                                                  CitlaliErrorV1 *) {
    static auto wrong = api;
    wrong.native_abi = 2;
    *out = &wrong;
    return CITLALI_OK;
}
namespace {
// Each broken descriptor tests the real dynamic loading path, before instance creation.
template <int Kind>
CitlaliStatus CITLALI_CALL broken_query(CitlaliStringView id, const CitlaliInterfaceV1 **out,
                                       CitlaliErrorV1 *e) {
    auto status = query(id, out, e);
    if (status != CITLALI_OK) return status;
    static CitlaliInterfaceV1 broken;
    broken = **out;
    if (Kind == 0) broken.protocol_id = cp::view("test.wrong/events-v1");
    if (Kind == 1) broken.protocol_version = 99;
    if (Kind == 2) broken.functions = nullptr;
    if (Kind == 3) broken.struct_size = 4;
    if (Kind == 4) broken.functions = &marker_v2;
    if (Kind == 5) broken.functions = &short_prefix;
    *out = &broken;
    return CITLALI_OK;
}
}
#define BROKEN_ENTRY(NAME, KIND) \
extern "C" CITLALI_EXPORT CitlaliStatus CITLALI_CALL NAME(uint32_t v, const CitlaliPluginApiV1 **out, CitlaliErrorV1 *e) { \
    auto status = citlali_plugin_entry(v, out, e); \
    if (status != CITLALI_OK) return status; \
    static auto broken = api; broken.query_interface = broken_query<KIND>; *out = &broken; return CITLALI_OK; \
}
BROKEN_ENTRY(wrong_protocol_entry, 0)
BROKEN_ENTRY(wrong_version_entry, 1)
BROKEN_ENTRY(null_functions_entry, 2)
BROKEN_ENTRY(short_descriptor_entry, 3)
BROKEN_ENTRY(wrong_prefix_entry, 4)
BROKEN_ENTRY(short_prefix_entry, 5)
