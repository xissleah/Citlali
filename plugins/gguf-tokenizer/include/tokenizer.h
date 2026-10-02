#ifndef CITLALI_CHAT_TOKENIZER_V1_H
#define CITLALI_CHAT_TOKENIZER_V1_H
#include "abi.h"
#define CITLALI_CHAT_TOKENIZER_PROTOCOL "example.gguf/chat-tokenizer-v1"
/* Prompt replaces history. Formats one user turn using the bound model chat template.
   Arrays/bytes are caller-owned; null/zero output queries required capacity.
   A piece is raw token bytes; runtime must assemble complete UTF-8 before emission.
   model_identity is borrowed until tokenizer destruction and verifies shared vocabulary. */
typedef struct CitlaliChatTokenizerV1 {
    uint32_t struct_size;
    uint32_t version;
    CitlaliStatus(CITLALI_CALL *model_identity)(CitlaliInstance, CitlaliStringView *, CitlaliErrorV1 *);
    CitlaliStatus(CITLALI_CALL *encode_chat)(CitlaliInstance, CitlaliStringView, int32_t *, uint64_t,
                                           uint64_t *, CitlaliErrorV1 *);
    CitlaliStatus(CITLALI_CALL *piece)(CitlaliInstance, int32_t, char *, uint64_t, uint64_t *, CitlaliErrorV1 *);
    CitlaliStatus(CITLALI_CALL *is_end)(CitlaliInstance, int32_t, uint32_t *, CitlaliErrorV1 *);
} CitlaliChatTokenizerV1;
#endif
