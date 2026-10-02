#ifndef CITLALI_FRONTEND_V1_H
#define CITLALI_FRONTEND_V1_H
#include "abi.h"
#define CITLALI_FRONTEND_PROTOCOL "citlali.frontend/service-v1"
typedef struct CitlaliFrontendV1 {
    uint32_t struct_size;
    uint32_t version;
    CitlaliStatus(CITLALI_CALL *start)(CitlaliInstance, CitlaliErrorV1 *);
    CitlaliStatus(CITLALI_CALL *request_stop)(CitlaliInstance, CitlaliErrorV1 *);
    CitlaliStatus(CITLALI_CALL *join)(CitlaliInstance, uint64_t timeout_ms, CitlaliErrorV1 *);
} CitlaliFrontendV1;
#endif
