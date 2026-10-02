#include "abi.h"
#include "frontend.h"
#include "text.h"
#include "probe.h"
static_assert(CITLALI_NATIVE_ABI == 1u, "first public native ABI");
#include <type_traits>
static_assert(std::is_standard_layout<CitlaliPluginApiV1>::value, "C layout");
static_assert(std::is_standard_layout<CitlaliInterfaceV1>::value, "descriptor C layout");
static_assert(std::is_trivially_copyable<CitlaliHostV1>::value, "host layout");
static_assert(sizeof(CitlaliStringView) == sizeof(void *) + sizeof(uint64_t), "64-bit target layout");
int main() {
    return 0;
}
