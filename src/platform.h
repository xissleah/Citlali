#pragma once
#include <filesystem>
#include <string>
namespace citlali {
void atomic_replace(const std::filesystem::path &, const std::filesystem::path &);
class Library {
    void *handle_ = nullptr;

  public:
    explicit Library(const std::filesystem::path &);
    ~Library();
    Library(const Library &) = delete;
    Library &operator=(const Library &) = delete;
    void *symbol(const std::string &) const;
};
} // namespace citlali
