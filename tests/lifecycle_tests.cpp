#include "host.h"
#include <fstream>
#include <iostream>
using namespace citlali;
void require(bool b, const char *m) {
    if (!b)
        throw std::runtime_error(m);
}
template <class F> void rejects(F f, const std::string &part) {
    try {
        f();
    } catch (const std::exception &e) {
        require(std::string(e.what()).find(part) != std::string::npos, e.what());
        return;
    }
    throw std::runtime_error("expected rejection: " + part);
}
std::string read(const fs::path &p) {
    std::ifstream f(p);
    return std::string((std::istreambuf_iterator<char>(f)), {});
}
int main(int argc, char **argv) {
    try {
        require(argc == 2, "dist argument");
        fs::path root = argv[1];
        auto trace = root / "lifecycle.trace";
#ifdef _WIN32
        _putenv_s("CITLALI_TEST_TRACE", trace.string().c_str());
#else
        setenv("CITLALI_TEST_TRACE", trace.c_str(), 1);
#endif
        auto reset = [&] { std::ofstream f(trace, std::ios::trunc); };
        auto manifest = read_manifest(root / "plugins/lifecycle");
        // Two nodes share a library but have distinct opaque instances. Synthetic registry isolates
        // lifecycle.
        Registry r;
        r.deployment.path = root / "list.citlali";
        r.order = {"provider", "consumer"};
        r.manifests["provider"] = manifest;
        r.manifests["consumer"] = manifest;
        r.selected["provider"] = {"provider", "name = 'provider'", {}};
        r.selected["consumer"] = {"consumer", "name = 'consumer'", {{"provider", {"provider"}}}};
        reset();
        {
            Host h(r);
            h.close();
        }
        auto s = read(trace);
        require(s.find("create provider") < s.find("create consumer"), "provider created first");
        require(s.find("destroy consumer") < s.find("destroy provider"), "consumer destroyed first");
        require(s.find("destroy provider") < s.find("unload"), "unload after instances");
        reset();
        auto failure = r;
        failure.selected["consumer"].config = "name = 'consumer'\nfail = true";
        rejects([&] { Host h(failure); }, "fixture create failure");
        s = read(trace);
        require(s.find("error.release") < s.find("destroy provider"),
                "foreign error released before rollback");
        require(s.find("destroy provider") < s.find("unload"), "rollback before unload");
        require(s.find("destroy consumer") == std::string::npos, "failed create self cleans");
        reset();
        auto bad = r;
        bad.manifests["consumer"].symbol = "incompatible_entry";
        rejects([&] { Host h(bad); }, "incompatible fixture entry");
        require(read(trace).find("create") == std::string::npos, "handshake before create");
        bad = r;
        bad.manifests["consumer"].symbol = "old_abi_entry";
        rejects([&] { Host h(bad); }, "incompatible API table");
        require(read(trace).find("create") == std::string::npos, "old ABI table rejected before create");
        bad = r;
        bad.manifests["consumer"].symbol = "mismatch_entry";
        rejects([&] { Host h(bad); }, "identity mismatch");
        bad = r;
        bad.manifests["consumer"].protocols.pop_back();
        rejects([&] { Host h(bad); }, "capability mismatch");
        bad = r;
        bad.selected["consumer"].config = "query_v2 = true";
        {
            Host h(bad);
            auto d = h.query_interface("provider", "test.lifecycle/events-v2");
            require(d->protocol_version == 2 &&
                        static_cast<const CitlaliTableV1 *>(d->functions)->version == 2,
                    "third-party v2 protocol and dependency query accepted");
            rejects([&] { h.query("provider", "test.lifecycle/events-v3"); }, "unsupported protocol");
        }
        for (auto item : std::vector<std::pair<std::string, std::string>>{
                 {"wrong_protocol_entry", "protocol ID mismatch"},
                 {"wrong_version_entry", "protocol version mismatch"},
                 {"null_functions_entry", "version/functions"},
                 {"short_descriptor_entry", "invalid interface descriptor"},
                 {"wrong_prefix_entry", "incompatible protocol prefix"},
                 {"short_prefix_entry", "incompatible protocol prefix"}}) {
            bad = r;
            bad.manifests["provider"].symbol = item.first;
            reset();
            rejects([&] { Host h(bad); }, item.second);
            require(read(trace).find("create") == std::string::npos, "bad descriptor rejected before create");
        }
        bad = r;
        bad.manifests["consumer"].probes = {{"demo.numeric", "gte", "8", "self"}};
        { Host h(bad, true, false); }
        bad.manifests["consumer"].probes[0].value = "11";
        rejects([&] { Host h(bad, false, false); }, "environment FAIL");
        bad.manifests["consumer"].probes = {{"demo.unknown", "eq", "1", "self"}};
        { Host h(bad, false, false); }
        rejects([&] { Host h(bad, true, false); }, "environment UNKNOWN");
        bad.manifests["consumer"].probes = {{"demo.numeric", "gte", "invalid", "self"}};
        rejects([&] { Host h(bad, false, false); }, "invalid numeric probe");
        fs::remove(trace);
        std::cout << "dynamic lifecycle contracts passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
