#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>
namespace citlali {
namespace fs = std::filesystem;
struct Dependency {
    std::string slot, protocol, cardinality;
    bool optional = false;
};
struct Probe {
    std::string id, op, value, checker;
};
struct Manifest {
    fs::path path, entry;
    std::string id, version, type, symbol, abi;
    std::vector<std::string> protocols, os, arch, advice;
    std::vector<Dependency> dependencies;
    std::vector<Probe> probes;
    std::string ref() const {
        return id + "@" + version;
    }
};
struct Inventory {
    std::string id, version;
    fs::path path;
    std::string ref() const {
        return id + "@" + version;
    }
};
struct Selection {
    std::string ref, config;
    std::map<std::string, std::vector<std::string>> bindings;
};
struct Deployment {
    fs::path path;
    std::vector<Inventory> inventory;
    std::vector<Selection> used;
};
struct Registry {
    Deployment deployment;
    std::map<std::string, Manifest> manifests;
    std::map<std::string, Selection> selected;
    std::vector<std::string> order, warnings;
};
Manifest read_manifest(const fs::path &, bool require_binary = true);
Deployment read_deployment(const fs::path &);
Registry validate(const Deployment &, bool strict_env = false, bool for_run = false);
void update_inventory(const fs::path &deployment, const fs::path &root);
void set_enabled(const fs::path &, const std::string &ref, bool enable);
std::string native_os();
std::string native_arch();
} // namespace citlali
