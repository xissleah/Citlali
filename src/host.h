#pragma once
#include "abi.h"
#include "config.h"
#include "platform.h"
#include <memory>
#include <mutex>
namespace citlali {
CitlaliStringView view(const std::string &);
CitlaliStringView view(const char *);
std::string string(CitlaliStringView);
CitlaliErrorV1 error_slot();
void require_status(CitlaliStatus, CitlaliErrorV1 &, const std::string &where);
class Host {
    struct Node;
    Registry registry_;
    std::vector<std::unique_ptr<Node>> nodes_;
    std::map<std::string, Node *> lookup_;
    std::mutex log_mutex_;
    bool closed_ = false;
    void load(bool strict_env);
    static void CITLALI_CALL log(void *, uint32_t, CitlaliStringView) noexcept;
    static uint64_t CITLALI_CALL count(void *, CitlaliStringView) noexcept;
    static CitlaliStatus CITLALI_CALL dependency(void *, CitlaliStringView, uint64_t, CitlaliInstance *,
                                                 CitlaliStringView *, CitlaliErrorV1 *) noexcept;
    static CitlaliStatus CITLALI_CALL query_dependency(void *, CitlaliStringView, uint64_t, CitlaliStringView,
                                                       const CitlaliInterfaceV1 **, CitlaliErrorV1 *) noexcept;

  public:
    explicit Host(Registry, bool strict_env = false, bool create = true);
    ~Host();
    Host(const Host &) = delete;
    void close();
    void run();
    CitlaliInstance instance(const std::string &) const;
    const void *query(const std::string &, const std::string &) const;
    const CitlaliInterfaceV1 *query_interface(const std::string &, const std::string &) const;
};
} // namespace citlali
