#ifndef CITLALI_NATIVE_ABI_V1_H
#define CITLALI_NATIVE_ABI_V1_H
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32)
#define CITLALI_CALL __cdecl
#define CITLALI_EXPORT __declspec(dllexport)
#else
#define CITLALI_CALL
#define CITLALI_EXPORT __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define CITLALI_NATIVE_ABI 1u
/* V1 suffixes below name structure generations, not the native ABI handshake.
   The first public native ABI uses interface descriptors for query callbacks. */
typedef int32_t CitlaliStatus;
#define CITLALI_OK 0
#define CITLALI_INVALID_ARGUMENT 1
#define CITLALI_UNSUPPORTED 2
#define CITLALI_INCOMPATIBLE_ABI 3
#define CITLALI_MISSING_DEPENDENCY 4
#define CITLALI_ENVIRONMENT_FAILED 5
#define CITLALI_BUSY 6
#define CITLALI_OUT_OF_MEMORY 7
#define CITLALI_CANCELLED 8
#define CITLALI_INTERNAL_ERROR 9
#define CITLALI_TIMEOUT 10
typedef struct CitlaliInstanceOpaque *CitlaliInstance;
typedef struct CitlaliStringView {
    const char *data;
    uint64_t length;
} CitlaliStringView;
typedef struct CitlaliErrorV1 {
    uint32_t struct_size;
    CitlaliStatus code;
    CitlaliStringView message;
    void *owner;
    void(CITLALI_CALL *release)(void *owner);
} CitlaliErrorV1;
typedef struct CitlaliTableV1 {
    uint32_t struct_size;
    uint32_t version;
} CitlaliTableV1;
/* Immutable, plugin-owned descriptor. Functions start with CitlaliTableV1;
   protocol_version describes the exact functional protocol, not the native ABI.
   Both descriptor and functions remain valid until the library is unloaded. */
typedef struct CitlaliInterfaceV1 {
    uint32_t struct_size;
    uint32_t protocol_version;
    CitlaliStringView protocol_id;
    const void *functions;
} CitlaliInterfaceV1;
typedef struct CitlaliHostV1 {
    uint32_t struct_size;
    uint32_t version;
    void *context;
    void(CITLALI_CALL *log)(void *, uint32_t level, CitlaliStringView);
    uint64_t(CITLALI_CALL *dependency_count)(void *, CitlaliStringView slot);
    CitlaliStatus(CITLALI_CALL *dependency)(void *, CitlaliStringView slot, uint64_t index, CitlaliInstance *,
                                            CitlaliStringView *identity, CitlaliErrorV1 *);
    CitlaliStatus(CITLALI_CALL *query_dependency)(void *, CitlaliStringView slot, uint64_t index,
                                                  CitlaliStringView protocol, const CitlaliInterfaceV1 **table,
                                                  CitlaliErrorV1 *);
    CitlaliStringView package_path;
    CitlaliStringView deployment_path;
} CitlaliHostV1;
/* config_toml is a UTF-8 TOML table, borrowed for create only. All callbacks use cdecl.
   Dependencies/context are host-owned and remain alive until instance destroy returns. */
typedef struct CitlaliPluginApiV1 {
    uint32_t struct_size;
    uint32_t native_abi;
    CitlaliStringView id;
    CitlaliStringView version;
    CitlaliStringView type;
    const CitlaliStringView *protocols;
    uint64_t protocol_count;
    CitlaliStatus(CITLALI_CALL *query_interface)(CitlaliStringView, const CitlaliInterfaceV1 **, CitlaliErrorV1 *);
    CitlaliStatus(CITLALI_CALL *create_instance)(const CitlaliHostV1 *, CitlaliStringView config_toml,
                                                 CitlaliInstance *, CitlaliErrorV1 *);
    CitlaliStatus(CITLALI_CALL *destroy_instance)(CitlaliInstance, CitlaliErrorV1 *);
} CitlaliPluginApiV1;
typedef CitlaliStatus(CITLALI_CALL *CitlaliEntryV1)(uint32_t, const CitlaliPluginApiV1 **, CitlaliErrorV1 *);
#ifdef __cplusplus
}
#endif
#endif
