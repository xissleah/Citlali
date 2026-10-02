#include "config.h"
#include "platform.h"
#include "toml.hpp"
#include <algorithm>
#include <fstream>
#include <functional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
namespace citlali {
namespace {
[[noreturn]] void fail(const std::string &w, const std::string &m) {
    throw std::runtime_error(w + ": " + m);
}
void keys(const toml::table &t, std::initializer_list<const char *> allowed, const std::string &w) {
    for (auto &&[k, v] : t) {
        bool ok = false;
        for (auto a : allowed)
            if (k.str() == a)
                ok = true;
        if (!ok)
            fail(w + "." + std::string(k.str()), "unknown field");
    }
}
std::string str(const toml::table &t, const char *k, const std::string &w) {
    auto v = t[k].value<std::string>();
    if (!v || v->empty())
        fail(w + "." + k, "expected nonempty string");
    return *v;
}
const toml::table &table(const toml::table &t, const char *k, const std::string &w) {
    auto n = t.get_as<toml::table>(k);
    if (!n)
        fail(w + "." + k, "expected table");
    return *n;
}
std::vector<std::string> strings(const toml::table &t, const char *k, const std::string &w,
                                 bool required = false, bool blank = false) {
    std::vector<std::string> out;
    auto n = t.get(k);
    if (!n) {
        if (required)
            fail(w + "." + k, "missing array");
        return out;
    }
    auto a = n->as_array();
    if (!a)
        fail(w + "." + k, "expected array");
    for (auto &x : *a) {
        auto s = x.value<std::string>();
        if (!s || (!blank && s->empty()))
            fail(w + "." + k, "expected nonempty string item");
        out.push_back(*s);
    }
    return out;
}
void identifier(const std::string &s, const std::string &w) {
    static const std::regex re("[a-z][a-z0-9_-]*(\\.[a-z][a-z0-9_-]*)*");
    if (s.size() > 128 || !std::regex_match(s, re))
        fail(w, "invalid lowercase identifier: " + s);
}
void version(const std::string &s, const std::string &w) {
    static const std::regex re("(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)");
    if (s.size() > 64 || !std::regex_match(s, re))
        fail(w, "expected exact major.minor.patch");
}
void protocol(const std::string &s, const std::string &w) {
    static const std::regex re("[a-z][a-z0-9_.-]*/[a-z][a-z0-9_/-]*-v[1-9][0-9]*");
    if (s.size() > 192 || !std::regex_match(s, re))
        fail(w, "invalid versioned protocol ID: " + s);
}
void reference(const std::string &s, const std::string &w) {
    auto p = s.find('@');
    if (p == std::string::npos)
        fail(w, "expected id@version");
    identifier(s.substr(0, p), w);
    version(s.substr(p + 1), w);
}
toml::table parse(const fs::path &p) {
    try {
        return toml::parse_file(p.string());
    } catch (const toml::parse_error &e) {
        std::ostringstream s;
        s << e;
        fail(p.string(), s.str());
    }
}
std::string render(const toml::table &t) {
    std::ostringstream s;
    s << toml::toml_formatter(t);
    return s.str();
}
bool within(const fs::path &c, const fs::path &p) {
    auto child = fs::weakly_canonical(c), parent = fs::weakly_canonical(p);
    auto ci = child.begin();
    for (auto pi = parent.begin(); pi != parent.end(); ++pi, ++ci)
        if (ci == child.end() || *ci != *pi)
            return false;
    return true;
}
const std::set<std::string> standard = {"runtime", "tokenizer", "model_load", "kernels", "cli", "server"};
void check_type(const std::string &s, const std::string &w) {
    if (standard.count(s))
        return;
    if (s == "Server")
        fail(w, "unknown type Server; use server");
    if (s.find('.') == std::string::npos)
        fail(w, "unknown standard type " + s + "; use server or namespace.name");
    identifier(s, w);
}
struct Lock {
    fs::path path;
    explicit Lock(fs::path p) : path(std::move(p)) {
        if (!fs::create_directory(path))
            fail(path.string(), "management update already locked");
    }
    ~Lock() {
        std::error_code ec;
        fs::remove(path, ec);
    }
};
void write_atomic(const fs::path &p, const toml::table &t) {
    auto tmp = p;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            fail(tmp.string(), "cannot write");
        out << toml::toml_formatter(t);
        out.flush();
        if (!out)
            fail(tmp.string(), "write failed");
    }
    try {
        atomic_replace(tmp, p);
    } catch (...) {
        std::error_code ec;
        fs::remove(tmp, ec);
        throw;
    }
}
} // namespace
std::string native_os() {
#ifdef _WIN32
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}
std::string native_arch() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#else
    return "unknown";
#endif
}
Manifest read_manifest(const fs::path &input, bool require_binary) {
    Manifest m;
    m.path = fs::weakly_canonical(input);
    auto w = m.path.string();
    for (auto f : {"info.toml", "protocol.h", "protocol.md"})
        if (!fs::is_regular_file(m.path / f))
            fail(w, std::string(f) + " missing");
    auto t = parse(m.path / "info.toml");
    keys(
        t,
        {"manifest_version", "plugin", "entry", "protocols", "dependencies", "strict_requirements", "advice"},
        w);
    if (t["manifest_version"].value<int64_t>() != 1 || !t["manifest_version"].is_integer())
        fail(w + ".manifest_version", "must be integer 1");
    auto &p = table(t, "plugin", w);
    keys(p, {"id", "version", "type", "display_name"}, w + ".plugin");
    m.id = str(p, "id", w + ".plugin");
    identifier(m.id, w + ".plugin.id");
    m.version = str(p, "version", w + ".plugin");
    version(m.version, w + ".plugin.version");
    m.type = str(p, "type", w + ".plugin");
    check_type(m.type, w + ".plugin.type");
    if (p.contains("display_name"))
        str(p, "display_name", w);
    auto &e = table(t, "entry", w);
    keys(e, {"path", "symbol", "abi"}, w + ".entry");
    fs::path entry = str(e, "path", w + ".entry");
    if (entry.is_absolute() || entry.has_root_name())
        fail(w + ".entry.path", "absolute entry forbidden");
    m.entry = fs::weakly_canonical(m.path / entry);
    if (!within(m.entry, m.path))
        fail(w + ".entry.path", "entry escapes package");
    if (require_binary && !fs::is_regular_file(m.entry))
        fail(w + ".entry.path", "binary missing: " + m.entry.string());
    m.symbol = str(e, "symbol", w + ".entry");
    m.abi = str(e, "abi", w + ".entry");
    if (m.abi != "citlali.native/v1")
        fail(w + ".entry.abi", "incompatible native ABI; entry will not be called");
    auto &ps = table(t, "protocols", w);
    keys(ps, {"provides"}, w + ".protocols");
    m.protocols = strings(ps, "provides", w + ".protocols", true);
    std::set<std::string> unique;
    for (auto &s : m.protocols) {
        protocol(s, w);
        if (!unique.insert(s).second)
            fail(w, "duplicate protocol");
    }
    if (auto n = t.get("dependencies")) {
        auto a = n->as_array();
        if (!a)
            fail(w, "dependencies must be array");
        std::set<std::string> slots;
        for (auto &x : *a) {
            auto d = x.as_table();
            if (!d)
                fail(w, "dependency must be table");
            keys(*d, {"slot", "protocol", "cardinality", "optional"}, w + ".dependencies");
            Dependency v;
            v.slot = str(*d, "slot", w);
            identifier(v.slot, w);
            v.protocol = str(*d, "protocol", w);
            protocol(v.protocol, w);
            v.cardinality = str(*d, "cardinality", w);
            if (v.cardinality != "one" && v.cardinality != "many")
                fail(w, "cardinality must be one or many");
            if (d->contains("optional")) {
                auto b = (*d)["optional"].value<bool>();
                if (!b)
                    fail(w, "optional must be bool");
                v.optional = *b;
            }
            if (!slots.insert(v.slot).second)
                fail(w, "duplicate dependency slot");
            m.dependencies.push_back(v);
        }
    }
    if (t.contains("strict_requirements")) {
        auto &s = table(t, "strict_requirements", w);
        keys(s, {"os", "arch", "probes"}, w + ".strict_requirements");
        m.os = strings(s, "os", w);
        m.arch = strings(s, "arch", w);
        for (auto &os : m.os)
            if (os != "windows" && os != "linux" && os != "macos")
                fail(w, "invalid OS requirement");
        for (auto &ar : m.arch)
            if (ar != "x86_64" && ar != "aarch64")
                fail(w, "invalid architecture requirement");
        if (auto n = s.get("probes")) {
            auto a = n->as_array();
            if (!a)
                fail(w, "probes must be array");
            for (auto &x : *a) {
                auto v = x.as_table();
                if (!v)
                    fail(w, "probe must be table");
                keys(*v, {"id", "op", "value", "checker"}, w + ".strict_requirements.probes");
                Probe p;
                p.id = str(*v, "id", w);
                identifier(p.id, w);
                p.op = str(*v, "op", w);
                p.value = str(*v, "value", w);
                p.checker = str(*v, "checker", w);
                if (p.op != "eq" && p.op != "ne" && p.op != "gte" && p.op != "lte" && p.op != "gt" &&
                    p.op != "lt")
                    fail(w, "invalid probe comparison operator");
                if (p.value.find_first_not_of(" \t\r\n") == std::string::npos ||
                    p.value == "PLUGIN_DEFINED_MINIMUM")
                    fail(w, "invalid probe value");
                if (p.id == "cuda.compute-capability" || p.id == "cuda.driver-version") {
                    static std::regex num("[0-9]+(\\.[0-9]+)*");
                    if (!std::regex_match(p.value, num))
                        fail(w, "invalid numeric probe value");
                }
                m.probes.push_back(p);
            }
        }
    }
    if (t.contains("advice")) {
        auto &a = table(t, "advice", w);
        keys(a, {"notes"}, w + ".advice");
        m.advice = strings(a, "notes", w, false, true);
        m.advice.erase(
            std::remove_if(m.advice.begin(), m.advice.end(),
                           [](auto &s) { return s.find_first_not_of(" \t\r\n") == std::string::npos; }),
            m.advice.end());
    }
    return m;
}
Deployment read_deployment(const fs::path &input) {
    Deployment d;
    d.path = fs::absolute(input).lexically_normal();
    auto t = parse(d.path);
    auto w = d.path.string();
    keys(t, {"all_plugins", "used_plugins"}, w);
    for (auto f : {"all_plugins", "used_plugins"})
        if (!t.get_as<toml::array>(f))
            fail(w + "." + f, "expected array (empty [] allowed)");
    for (auto &x : *t.get_as<toml::array>("all_plugins")) {
        auto p = x.as_table();
        if (!p)
            fail(w, "inventory item must be table");
        keys(*p, {"id", "version", "path"}, w + ".all_plugins");
        Inventory i;
        i.id = str(*p, "id", w);
        identifier(i.id, w);
        i.version = str(*p, "version", w);
        version(i.version, w);
        i.path = fs::weakly_canonical(d.path.parent_path() / fs::path(str(*p, "path", w)));
        d.inventory.push_back(i);
    }
    for (auto &x : *t.get_as<toml::array>("used_plugins")) {
        auto p = x.as_table();
        if (!p)
            fail(w, "selection must be table");
        keys(*p, {"ref", "bindings", "config"}, w + ".used_plugins");
        Selection s;
        s.ref = str(*p, "ref", w);
        reference(s.ref, w);
        if (p->contains("bindings")) {
            auto &b = table(*p, "bindings", w);
            for (auto &&[k, v] : b) {
                identifier(std::string(k.str()), w);
                auto a = v.as_array();
                if (!a)
                    fail(w, "binding must be array");
                std::vector<std::string> refs;
                for (auto &x : *a) {
                    auto ref = x.value<std::string>();
                    if (!ref)
                        fail(w, "invalid binding ref");
                    reference(*ref, w);
                    refs.push_back(*ref);
                }
                s.bindings.emplace(std::string(k.str()), refs);
            }
        }
        if (p->contains("config"))
            s.config = render(table(*p, "config", w));
        d.used.push_back(s);
    }
    return d;
}
Registry validate(const Deployment &d, bool strict_env, bool for_run) {
    Registry r;
    r.deployment = d;
    std::map<std::string, Inventory> inv;
    std::set<fs::path> paths;
    for (auto &i : d.inventory) {
        if (!inv.emplace(i.ref(), i).second)
            fail(i.ref(), "duplicate inventory identity");
        if (!paths.insert(i.path).second)
            fail(i.ref(), "duplicate package path");
    }
    std::map<std::string, std::string> ids, types;
    for (auto &s : d.used) {
        if (!r.selected.emplace(s.ref, s).second)
            fail(s.ref, "duplicate enabled identity");
        auto ii = inv.find(s.ref);
        if (ii == inv.end())
            fail(s.ref, "not in inventory");
        auto m = read_manifest(ii->second.path);
        if (m.ref() != s.ref)
            fail(s.ref, "inventory and manifest identity differ");
        if (!ids.emplace(m.id, m.version).second)
            fail(s.ref, "multiple versions enabled");
        if (standard.count(m.type) && m.type != "kernels" && !types.emplace(m.type, s.ref).second)
            fail(s.ref, "type conflict: " + m.type);
        for (auto &pair : std::vector<std::pair<std::vector<std::string>, std::string>>{
                 {m.os, native_os()}, {m.arch, native_arch()}})
            if (!pair.first.empty() &&
                std::find(pair.first.begin(), pair.first.end(), pair.second) == pair.first.end())
                fail(s.ref, "environment FAIL: " + pair.second);
        if (!m.probes.empty()) {
            r.warnings.push_back(s.ref + ": environment UNKNOWN until explicit --probe/run");
            if (strict_env)
                fail(s.ref, "environment UNKNOWN (--strict-env static check)");
        }
        if (m.os.empty() && m.arch.empty() && m.probes.empty() && m.advice.empty())
            r.warnings.push_back(s.ref + ": strict_requirements and advice are empty");
        for (auto &note : m.advice)
            r.warnings.push_back(s.ref + ": advice: " + note);
        r.manifests.emplace(s.ref, std::move(m));
    }
    std::set<std::string> referenced;
    for (auto &[ref, s] : r.selected) {
        auto &m = r.manifests.at(ref);
        if (m.type == "cli" || m.type == "server")
            referenced.insert(ref);
        for (auto &[slot, targets] : s.bindings) {
            auto decl = std::find_if(m.dependencies.begin(), m.dependencies.end(),
                                     [&](auto &d) { return d.slot == slot; });
            if (decl == m.dependencies.end())
                fail(ref + ".bindings." + slot, "slot not declared");
            std::set<std::string> uniq;
            for (auto &target : targets) {
                if (!uniq.insert(target).second)
                    fail(ref, "duplicate binding " + target);
                auto mi = r.manifests.find(target);
                if (mi == r.manifests.end())
                    fail(ref + ".bindings." + slot, "target not enabled: " + target);
                if (std::find(mi->second.protocols.begin(), mi->second.protocols.end(), decl->protocol) ==
                    mi->second.protocols.end())
                    fail(ref + ".bindings." + slot,
                         "protocol mismatch: " + decl->protocol + " from " + target);
                referenced.insert(target);
            }
        }
        for (auto &dep : m.dependencies) {
            auto b = s.bindings.find(dep.slot);
            size_t count = b == s.bindings.end() ? 0 : b->second.size();
            if (!dep.optional && !count)
                fail(ref + ".bindings." + dep.slot, "missing required dependency");
            if (dep.cardinality == "one" && count > 1)
                fail(ref + ".bindings." + dep.slot, "cardinality one exceeded");
        }
    }
    std::map<std::string, int> color;
    std::vector<std::string> stack;
    std::function<void(const std::string &)> visit = [&](auto &ref) {
        if (color[ref] == 2)
            return;
        if (color[ref] == 1) {
            std::string cycle;
            for (auto &x : stack)
                cycle += x + " -> ";
            fail(ref, "dependency cycle: " + cycle + ref);
        }
        color[ref] = 1;
        stack.push_back(ref);
        for (auto &[slot, targets] : r.selected.at(ref).bindings)
            for (auto &target : targets)
                visit(target);
        stack.pop_back();
        color[ref] = 2;
        r.order.push_back(ref);
    };
    for (auto &s : d.used)
        visit(s.ref);
    for (auto &[ref, m] : r.manifests)
        if (!referenced.count(ref))
            fail(ref, "enabled plugin is not connected to deployment");
    if (for_run) {
        if (!types.count("runtime"))
            fail(d.path.string(), "run requires one runtime");
        if (!types.count("cli") && !types.count("server"))
            fail(d.path.string(), "run requires a frontend");
    }
    return r;
}
void update_inventory(const fs::path &input, const fs::path &root) {
    auto p = fs::absolute(input);
    Lock lock(p.string() + ".lock");
    auto d = read_deployment(p);
    auto t = parse(p);
    auto a = t.get_as<toml::array>("all_plugins");
    std::map<std::string, fs::path> existing;
    for (auto &i : d.inventory)
        if (!existing.emplace(i.ref(), i.path).second)
            fail(i.ref(), "duplicate inventory identity");
    std::vector<fs::path> dirs;
    for (auto &e : fs::directory_iterator(root))
        if (e.is_directory() && fs::is_regular_file(e.path() / "info.toml"))
            dirs.push_back(e.path());
    std::sort(dirs.begin(), dirs.end());
    for (auto &dir : dirs) {
        auto m = read_manifest(dir, false);
        auto old = existing.find(m.ref());
        if (old != existing.end()) {
            if (old->second != m.path)
                fail(m.ref(), "scan identity exists at another path");
            continue;
        }
        existing[m.ref()] = m.path;
        toml::table item;
        item.insert("id", m.id);
        item.insert("version", m.version);
        item.insert("path", fs::relative(m.path, p.parent_path()).generic_string());
        a->push_back(std::move(item));
    }
    write_atomic(p, t);
}
void set_enabled(const fs::path &input, const std::string &ref, bool enable) {
    reference(ref, "selection");
    auto p = fs::absolute(input);
    Lock lock(p.string() + ".lock");
    auto d = read_deployment(p);
    auto t = parse(p);
    auto a = t.get_as<toml::array>("used_plugins");
    if (enable) {
        if (std::none_of(d.inventory.begin(), d.inventory.end(), [&](auto &i) { return i.ref() == ref; }))
            fail(ref, "not in inventory");
        if (std::none_of(d.used.begin(), d.used.end(), [&](auto &s) { return s.ref == ref; })) {
            toml::table item;
            item.insert("ref", ref);
            a->push_back(std::move(item));
        }
    } else {
        for (size_t i = a->size(); i > 0; --i)
            if ((*a)[i - 1].as_table()->get_as<std::string>("ref")->get() == ref)
                a->erase(a->begin() + static_cast<ptrdiff_t>(i - 1));
    }
    write_atomic(p, t);
}
} // namespace citlali
