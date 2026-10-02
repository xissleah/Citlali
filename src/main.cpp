#include "config.h"
#include "host.h"
#include <iostream>
#include <stdexcept>
int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--version") {
            std::cout << "Citlali v0.1\n";
            return 0;
        }
        if (argc < 2)
            throw std::runtime_error("usage: citlali inspect|list|scan|enable|disable|check|run ...");
        std::string command = argv[1], deployment = "list.citlali", arg;
        bool probe = false, strict = false;
        for (int i = 2; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--deployment") {
                if (++i == argc)
                    throw std::runtime_error("missing deployment path");
                deployment = argv[i];
            } else if (a == "--probe")
                probe = true;
            else if (a == "--strict-env")
                strict = true;
            else if (a.rfind("--", 0) == 0)
                throw std::runtime_error("unknown option " + a);
            else if (arg.empty())
                arg = a;
            else
                throw std::runtime_error("unexpected argument " + a);
        }
        if (command == "inspect") {
            if (arg.empty())
                throw std::runtime_error("inspect needs package path");
            auto m = citlali::read_manifest(arg);
            std::cout << m.ref() << " type=" << m.type << " entry=" << m.entry.string() << "\n";
            for (auto &p : m.protocols)
                std::cout << "  " << p << "\n";
        } else if (command == "scan") {
            if (arg.empty())
                throw std::runtime_error("scan needs plugin root");
            citlali::update_inventory(deployment, arg);
            std::cout << "inventory updated; selections preserved\n";
        } else if (command == "enable" || command == "disable") {
            if (arg.empty())
                throw std::runtime_error("selection needs id@version");
            citlali::set_enabled(deployment, arg, command == "enable");
            std::cout << "selection saved; run check to validate bindings\n";
        } else if (command == "list") {
            auto d = citlali::read_deployment(deployment);
            for (auto &i : d.inventory) {
                bool used = false;
                for (auto &s : d.used)
                    if (s.ref == i.ref())
                        used = true;
                std::cout << (used ? "enabled " : "inventory ") << i.ref() << " " << i.path.string()
                          << (citlali::fs::exists(i.path / "info.toml") ? "" : " [missing]") << "\n";
            }
        } else if (command == "check" || command == "run") {
            if (arg.empty())
                arg = deployment;
            bool dynamic = probe || command == "run";
            auto r = citlali::validate(citlali::read_deployment(arg), strict && !dynamic, command == "run");
            for (auto &w : r.warnings)
                std::cerr << "warning: " << w << "\n";
            auto size = r.order.size();
            if (dynamic) {
                citlali::Host host(std::move(r), strict, command == "run");
                if (command == "run")
                    host.run();
                else
                    host.close();
            }
            std::cout << (dynamic ? "dynamic" : "static") << " check OK: " << size << " enabled plugins\n";
        } else
            throw std::runtime_error("unknown command " + command);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
