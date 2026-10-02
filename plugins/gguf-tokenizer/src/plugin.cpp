#include "../protocol.h"
#include "../include/model.h"
#include "plugin_support.h"
#include "toml.hpp"
#include <memory>
namespace {
struct Tokenizer {
    CitlaliInstance model;
    const CitlaliGgufV1 *vocab;
    std::string identity;
    const CitlaliChatFormatV1 *chat;
};
Tokenizer *tokenizer(CitlaliInstance h) {
    cp::need(h, "null tokenizer");
    return reinterpret_cast<Tokenizer *>(h);
}
CitlaliStatus CITLALI_CALL identity(CitlaliInstance h, CitlaliStringView *out, CitlaliErrorV1 *e) {
    return cp::guard(e, [&] { cp::need(out, "null model identity"); *out = cp::view(tokenizer(h)->identity); });
}
CitlaliStatus CITLALI_CALL encode(CitlaliInstance h, CitlaliStringView input, int32_t *out,
                                 uint64_t capacity, uint64_t *required, CitlaliErrorV1 *e) {
    return cp::guard(e, [&] {
        auto t = tokenizer(h);
        auto prompt = cp::string(input);
        cp::need(cp::valid_utf8(prompt) && prompt.find('\0') == std::string::npos, "prompt must be UTF-8 without NUL");
        auto error = cp::err();
        uint64_t size = 0;
        cp::check(t->chat->format_chat(t->model, cp::view(prompt), nullptr, 0, &size, &error), error);
        cp::need(size <= 4 * 1024 * 1024, "formatted prompt exceeds budget");
        std::string chat(size, '\0');
        cp::check(t->chat->format_chat(t->model, cp::view(prompt), chat.data(), chat.size(), &size, &error), error);
        cp::check(t->vocab->tokenize(t->model, cp::view(chat), out, capacity, required, &error), error);
    });
}
CitlaliStatus CITLALI_CALL piece(CitlaliInstance h, int32_t token, char *out, uint64_t capacity,
                                uint64_t *required, CitlaliErrorV1 *e) {
    return cp::guard(e, [&] {
        auto t = tokenizer(h); auto error = cp::err();
        cp::check(t->vocab->piece(t->model, token, out, capacity, required, &error), error);
    });
}
CitlaliStatus CITLALI_CALL is_end(CitlaliInstance h, int32_t token, uint32_t *out, CitlaliErrorV1 *e) {
    return cp::guard(e, [&] {
        auto t = tokenizer(h); auto error = cp::err();
        cp::check(t->vocab->is_end(t->model, token, out, &error), error);
    });
}
CitlaliStatus CITLALI_CALL create(const CitlaliHostV1 *host, CitlaliStringView config,
                                 CitlaliInstance *out, CitlaliErrorV1 *e) {
    if (out) *out = nullptr;
    return cp::guard(e, [&] {
        cp::need(out && host && host->struct_size >= sizeof(*host) && host->version == 1, "invalid host");
        cp::need(toml::parse(cp::string(config)).empty(), "tokenizer config must be empty");
        auto t = std::make_unique<Tokenizer>();
        t->vocab = cp::query<CitlaliGgufV1>(*host, "model", 0, CITLALI_GGUF_PROTOCOL, &t->model, &t->identity);
        cp::need(t->vocab->architecture && t->vocab->tokenize && t->vocab->piece && t->vocab->is_end,
                 "incomplete vocabulary interface", CITLALI_INCOMPATIBLE_ABI);
        CitlaliInstance chat_model = nullptr;
        t->chat = cp::query<CitlaliChatFormatV1>(*host, "model", 0, CITLALI_CHAT_FORMAT_PROTOCOL, &chat_model);
        cp::need(chat_model == t->model && t->chat->format_chat, "chat/vocab model mismatch");
        uint64_t probe_size = 0; auto error = cp::err();
        cp::check(t->chat->format_chat(t->model, cp::view("Hello"), nullptr, 0, &probe_size, &error), error);
        *out = reinterpret_cast<CitlaliInstance>(t.release());
    });
}
CitlaliStatus CITLALI_CALL destroy(CitlaliInstance h, CitlaliErrorV1 *e) {
    return cp::guard(e, [&] { delete tokenizer(h); });
}
const CitlaliChatTokenizerV1 table{sizeof(table), 1, identity, encode, piece, is_end};
CitlaliStatus CITLALI_CALL query(CitlaliStringView id, const CitlaliInterfaceV1 **out, CitlaliErrorV1 *e) {
    if (out) *out = nullptr;
    return cp::guard(e, [&] {
        cp::need(out, "null query output");
        cp::need(cp::string(id) == CITLALI_CHAT_TOKENIZER_PROTOCOL, "unsupported tokenizer interface", CITLALI_UNSUPPORTED);
        static const auto d = cp::interface(CITLALI_CHAT_TOKENIZER_PROTOCOL, &table); *out = &d;
    });
}
const CitlaliStringView protocols[]{cp::view(CITLALI_CHAT_TOKENIZER_PROTOCOL)};
const CitlaliPluginApiV1 api{sizeof(api), CITLALI_NATIVE_ABI, cp::view("example.gguf-tokenizer"),
                            cp::view("0.1.0"), cp::view("tokenizer"), protocols, 1, query, create, destroy};
}
extern "C" CITLALI_EXPORT CitlaliStatus CITLALI_CALL citlali_plugin_entry(
    uint32_t v, const CitlaliPluginApiV1 **out, CitlaliErrorV1 *e) {
    if (out) *out = nullptr;
    return cp::guard(e, [&] { cp::need(out && v == CITLALI_NATIVE_ABI, "incompatible native ABI", CITLALI_INCOMPATIBLE_ABI); *out = &api; });
}
