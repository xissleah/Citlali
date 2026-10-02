#include "abi.h"
#include "frontend.h"
#include "text.h"
#include "probe.h"
_Static_assert(CITLALI_NATIVE_ABI == 1u, "first public native ABI");
_Static_assert(sizeof(CitlaliStatus) == 4, "status width");
_Static_assert(sizeof(CitlaliTableV1) == 8, "table prefix");
_Static_assert(offsetof(CitlaliErrorV1, code) == 4, "error prefix");
_Static_assert(offsetof(CitlaliPluginApiV1, native_abi) == 4, "API prefix");
_Static_assert(offsetof(CitlaliInterfaceV1, protocol_version) == 4, "descriptor prefix");
int main(void) {
    return CITLALI_OK;
}
