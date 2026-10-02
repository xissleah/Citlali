#ifndef CITLALI_ENVIRONMENT_PROBE_V1_H
#define CITLALI_ENVIRONMENT_PROBE_V1_H
#include "abi.h"
#define CITLALI_PROBE_PROTOCOL "citlali.environment/probe-v1"
#define CITLALI_ENV_PASS 1u
#define CITLALI_ENV_FAIL 2u
#define CITLALI_ENV_UNKNOWN 3u
typedef struct CitlaliProbeV1 {
    uint32_t struct_size;
    uint32_t version;
    CitlaliStatus(CITLALI_CALL *evaluate)(CitlaliStringView id, CitlaliStringView op, CitlaliStringView value,
                                       uint32_t *result, CitlaliErrorV1 *);
} CitlaliProbeV1;
#endif
