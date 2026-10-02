#ifndef CITLALI_GGUF_MODEL_V1_H
#define CITLALI_GGUF_MODEL_V1_H
#include "abi.h"
#define CITLALI_GGUF_PROTOCOL "citlali.model/gguf-vocab-v1"
#define CITLALI_DECODE_PROTOCOL "citlali.compute/token-decode-v1"
typedef struct CitlaliDecoderOpaque *CitlaliDecoder;
/* All buffers are caller-owned. A null/zero output queries required capacity.
   If capacity is insufficient, required is set and UNSUPPORTED returned with no writes.
   Pieces are raw token bytes, which need not independently form valid UTF-8. */
typedef struct CitlaliGgufV1 {
    uint32_t struct_size;
    uint32_t version;
    CitlaliStatus(CITLALI_CALL *architecture)(CitlaliInstance, CitlaliStringView *, CitlaliErrorV1 *);
    CitlaliStatus(CITLALI_CALL *tokenize)(CitlaliInstance, CitlaliStringView, int32_t *, uint64_t capacity,
                                        uint64_t *required, CitlaliErrorV1 *);
    CitlaliStatus(CITLALI_CALL *piece)(CitlaliInstance, int32_t token, char *, uint64_t capacity,
                                     uint64_t *required, CitlaliErrorV1 *);
    CitlaliStatus(CITLALI_CALL *is_end)(CitlaliInstance, int32_t token, uint32_t *, CitlaliErrorV1 *);
} CitlaliGgufV1;
/* Decoder owns KV cache, borrowed model outlives decoder. Caller serializes methods.
   decode consumes tokens, keeps last-position logits; greedy selects next token.
   reset clears all previous prompt state. No C++ or libllama structs cross this ABI. */
typedef struct CitlaliDecodeV1 {
    uint32_t struct_size;
    uint32_t version;
    CitlaliStatus(CITLALI_CALL *prepare)(CitlaliInstance, uint32_t context_size, uint32_t threads,
                                        CitlaliDecoder *, CitlaliErrorV1 *);
    CitlaliStatus(CITLALI_CALL *reset)(CitlaliInstance, CitlaliDecoder, CitlaliErrorV1 *);
    CitlaliStatus(CITLALI_CALL *decode)(CitlaliInstance, CitlaliDecoder, const int32_t *, uint64_t count,
                                       CitlaliErrorV1 *);
    CitlaliStatus(CITLALI_CALL *greedy)(CitlaliInstance, CitlaliDecoder, int32_t *, CitlaliErrorV1 *);
    CitlaliStatus(CITLALI_CALL *release)(CitlaliInstance, CitlaliDecoder, CitlaliErrorV1 *);
} CitlaliDecodeV1;
#define CITLALI_CHAT_FORMAT_PROTOCOL "example.gguf/chat-format-v1"
typedef struct CitlaliChatFormatV1 {
    uint32_t struct_size;
    uint32_t version;
    CitlaliStatus(CITLALI_CALL *format_chat)(CitlaliInstance, CitlaliStringView, char *, uint64_t capacity,
                                          uint64_t *required, CitlaliErrorV1 *);
} CitlaliChatFormatV1;
#endif
