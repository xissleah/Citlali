#include "platform.h"
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace citlali {
Library::Library(const std::filesystem::path &p) {
#ifdef _WIN32
    handle_ = LoadLibraryExW(p.c_str(), nullptr,
                             LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!handle_)
        throw std::runtime_error("LoadLibrary " + p.string() + " failed, Windows error " +
                                 std::to_string(GetLastError()));
#else
    handle_ = dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle_)
        throw std::runtime_error("dlopen " + p.string() + ": " + dlerror());
#endif
}
Library::~Library() {
    if (handle_) {
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(handle_));
#else
        dlclose(handle_);
#endif
    }
}
void *Library::symbol(const std::string &s) const {
#ifdef _WIN32
    auto result = reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(handle_), s.c_str()));
#else
    auto result = dlsym(handle_, s.c_str());
#endif
    if (!result)
        throw std::runtime_error("missing native symbol: " + s);
    return result;
}
void atomic_replace(const std::filesystem::path &from, const std::filesystem::path &to) {
#ifdef _WIN32
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("atomic replace failed: " + std::to_string(GetLastError()));
#else
    std::filesystem::rename(from, to);
#endif
}
} // namespace citlali
