#ifndef CITLALI_TEXT_V1_H
#define CITLALI_TEXT_V1_H
#include "abi.h"
#define CITLALI_TEXT_PROTOCOL "citlali.inference/text-v1"
#define CITLALI_EVENT_TEXT 1u
#define CITLALI_EVENT_COMPLETED 2u
#define CITLALI_EVENT_CANCELLED 3u
#define CITLALI_EVENT_FAILED 4u
#define CITLALI_OPERATION_RUNNING 1u
#define CITLALI_OPERATION_COMPLETED 2u
#define CITLALI_OPERATION_CANCELLED 3u
#define CITLALI_OPERATION_FAILED 4u
typedef struct CitlaliSessionOpaque *CitlaliSession;
typedef struct CitlaliOperationOpaque *CitlaliOperation;
typedef struct CitlaliTextRequestV1 {
    uint32_t struct_size;
    uint32_t max_tokens;
    CitlaliStringView prompt;
} CitlaliTextRequestV1;
typedef struct CitlaliTextEventV1 {
    uint32_t struct_size;
    uint32_t kind;
    CitlaliStringView text;
    uint64_t generated_units;
} CitlaliTextEventV1;
typedef uint32_t(CITLALI_CALL *CitlaliTextSinkV1)(void *, const CitlaliTextEventV1 *);
typedef struct CitlaliTextV1 {
    uint32_t struct_size;
    uint32_t version;
    CitlaliStatus(CITLALI_CALL *create_session)(CitlaliInstance, CitlaliSession *, CitlaliErrorV1 *);
    CitlaliStatus(CITLALI_CALL *destroy_session)(CitlaliInstance, CitlaliSession, CitlaliErrorV1 *);
    CitlaliStatus(CITLALI_CALL *submit)(CitlaliInstance, CitlaliSession, const CitlaliTextRequestV1 *,
                                        CitlaliTextSinkV1, void *, CitlaliOperation *, CitlaliErrorV1 *);
    CitlaliStatus(CITLALI_CALL *wait)(CitlaliInstance, CitlaliOperation, uint64_t timeout_ms, uint32_t *state,
                                      CitlaliErrorV1 *);
    CitlaliStatus(CITLALI_CALL *cancel)(CitlaliInstance, CitlaliOperation, CitlaliErrorV1 *);
    CitlaliStatus(CITLALI_CALL *release_operation)(CitlaliInstance, CitlaliOperation, CitlaliErrorV1 *);
    CitlaliStatus(CITLALI_CALL *request_stop)(CitlaliInstance, CitlaliErrorV1 *);
    CitlaliStatus(CITLALI_CALL *plan_summary)(CitlaliInstance, CitlaliStringView *, CitlaliErrorV1 *);
} CitlaliTextV1;
#endif
