#include "../protocol.h"
#include "platform.h"
#include "plugin_support.h"
#include "toml.hpp"
#include "llama.h"
#include <algorithm>
#include <array>
#include <climits>
#include <filesystem>
#include <memory>
#include <set>
#include <vector>
#ifndef CITLALI_MODEL_PLUGIN_ID
#define CITLALI_MODEL_PLUGIN_ID "example.gguf-model"
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace {
// Bind only the C API: the host and other plugins do not link against libllama.
struct Api {
    citlali::Library base, ggml, llama;
#define API(NAME) decltype(&::NAME) NAME = reinterpret_cast<decltype(&::NAME)>(llama.symbol(#NAME))
    API(llama_model_default_params);
    API(llama_context_default_params);
    API(llama_backend_init);
    API(llama_log_set);
    API(llama_model_load_from_file);
    API(llama_model_free);
    API(llama_model_get_vocab);
    API(llama_model_meta_val_str);
    API(llama_model_chat_template);
    API(llama_chat_apply_template);
    API(llama_vocab_n_tokens);
    API(llama_tokenize);
    API(llama_token_to_piece);
    API(llama_vocab_is_eog);
    API(llama_init_from_model);
    API(llama_free);
    API(llama_n_ctx);
    API(llama_n_batch);
    API(llama_get_memory);
    API(llama_memory_clear);
    API(llama_batch_get_one);
    API(llama_decode);
    API(llama_sampler_init_greedy);
    API(llama_sampler_chain_init);
    API(llama_sampler_chain_default_params);
    API(llama_sampler_chain_add);
    API(llama_sampler_sample);
    API(llama_sampler_reset);
    API(llama_sampler_free);
#undef API
    explicit Api(const std::filesystem::path &bin)
        : base(bin / "ggml-base.dll"), ggml(bin / "ggml.dll"), llama(bin / "llama.dll") {
        llama_backend_init();
    }
};
Api &process_api(const std::filesystem::path &bin) {
    // Upstream GGML cannot safely unload dynamic backends (registry threads/global
    // resources). Retain this one matched C API/DLL set for the process lifetime.
    // Model/context allocations still have normal per-instance ownership.
    static const auto resident = std::filesystem::weakly_canonical(bin);
    cp::need(resident == std::filesystem::weakly_canonical(bin),
             "another CPU backend package is resident; use a fresh process");
    static Api *shared = new Api(bin);
    return *shared;
}
void quiet_log(enum ggml_log_level level, const char *text, void *) noexcept {
    if (level >= GGML_LOG_LEVEL_WARN) std::fputs(text, stderr);
}
struct Decoder {
    Api &api;
    llama_context *context = nullptr;
    llama_sampler *sampler = nullptr;
    uint64_t used = 0;
    bool logits = false;
    explicit Decoder(Api &a) : api(a) {}
    ~Decoder() {
        if (context) api.llama_free(context);
        if (sampler) api.llama_sampler_free(sampler);
    }
};
struct Model {
    Api &api;
    llama_model *model = nullptr;
    const llama_vocab *vocab = nullptr;
    std::string architecture;
    std::set<Decoder *> decoders;
    explicit Model(const std::filesystem::path &bin) : api(process_api(bin)) {}
    ~Model() {
        for (auto d : decoders) delete d;
        if (model) api.llama_model_free(model);
        // GGML backend DLLs retain a global logger; remove pointers into this plugin
        // before its library can be unloaded by the host.
        api.llama_log_set(nullptr, nullptr);
    }
};
Model *model(CitlaliInstance h) {
    cp::need(h, "null model");
    return reinterpret_cast<Model *>(h);
}
Decoder *decoder(Model *m, CitlaliDecoder h) {
    auto d = reinterpret_cast<Decoder *>(h);
    cp::need(m->decoders.count(d), "decoder not owned by model");
    return d;
}
void token(Model *m, int32_t t) {
    cp::need(t >= 0 && t < m->api.llama_vocab_n_tokens(m->vocab), "invalid token ID");
}
CitlaliStatus CITLALI_CALL architecture(CitlaliInstance h, CitlaliStringView *out, CitlaliErrorV1 *e) {
    return cp::guard(e, [&] { cp::need(out, "null architecture"); *out = cp::view(model(h)->architecture); });
}
CitlaliStatus CITLALI_CALL tokenize(CitlaliInstance h, CitlaliStringView input, int32_t *out,
                                   uint64_t capacity, uint64_t *required, CitlaliErrorV1 *e) {
    return cp::guard(e, [&] {
        auto m = model(h);
        cp::need(required && input.length <= INT_MAX && capacity <= INT_MAX && (!capacity || out),
                 "invalid tokenize buffer");
        auto text = cp::string(input);
        cp::need(cp::valid_utf8(text), "tokenize input must be UTF-8");
        int32_t n = m->api.llama_tokenize(m->vocab, text.data(), static_cast<int32_t>(text.size()),
                                         nullptr, 0, true, true);
        cp::need(n != INT_MIN, "token count overflow");
        *required = n < 0 ? -int64_t(n) : n;
        if (!out && !capacity) return;
        cp::need(capacity >= *required, "tokenize capacity insufficient", CITLALI_UNSUPPORTED);
        n = m->api.llama_tokenize(m->vocab, text.data(), static_cast<int32_t>(text.size()), out,
                                  static_cast<int32_t>(capacity), true, true);
        cp::need(n >= 0, "libllama tokenize failed", CITLALI_INTERNAL_ERROR);
        *required = n;
    });
}
CitlaliStatus CITLALI_CALL piece(CitlaliInstance h, int32_t t, char *out, uint64_t capacity,
                                uint64_t *required, CitlaliErrorV1 *e) {
    return cp::guard(e, [&] {
        auto m = model(h);
        token(m, t);
        cp::need(required && capacity <= INT_MAX && (!capacity || out), "invalid piece buffer");
        int32_t n = m->api.llama_token_to_piece(m->vocab, t, nullptr, 0, 0, false);
        cp::need(n != INT_MIN, "piece overflow");
        *required = n < 0 ? -int64_t(n) : n;
        if (!out && !capacity) return;
        cp::need(capacity >= *required, "piece capacity insufficient", CITLALI_UNSUPPORTED);
        n = m->api.llama_token_to_piece(m->vocab, t, out, static_cast<int32_t>(capacity), 0, false);
        cp::need(n >= 0, "libllama piece failed", CITLALI_INTERNAL_ERROR);
        *required = n;
    });
}
CitlaliStatus CITLALI_CALL is_end(CitlaliInstance h, int32_t t, uint32_t *out, CitlaliErrorV1 *e) {
    return cp::guard(e, [&] {
        auto m = model(h); token(m, t); cp::need(out, "null end output");
        *out = m->api.llama_vocab_is_eog(m->vocab, t) ? 1u : 0u;
    });
}
CitlaliStatus CITLALI_CALL prepare(CitlaliInstance h, uint32_t size, uint32_t threads,
                                  CitlaliDecoder *out, CitlaliErrorV1 *e) {
    if (out) *out = nullptr;
    return cp::guard(e, [&] {
        auto m = model(h);
        cp::need(out && size >= 128 && size <= 32768 && threads >= 1 && threads <= 256,
                 "invalid decoder settings");
        cp::need(m->decoders.empty(), "one decoder supported per model", CITLALI_BUSY);
        auto d = std::make_unique<Decoder>(m->api);
        auto params = m->api.llama_context_default_params();
        params.n_ctx = size;
        params.n_batch = std::min<uint32_t>(512, size);
        params.n_ubatch = std::min<uint32_t>(128, params.n_batch);
        params.n_seq_max = 1;
        params.n_threads = params.n_threads_batch = threads;
        params.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_AUTO;
        d->sampler = m->api.llama_sampler_chain_init(m->api.llama_sampler_chain_default_params());
        cp::need(d->sampler, "sampler allocation failed", CITLALI_OUT_OF_MEMORY);
        auto greedy_sampler = m->api.llama_sampler_init_greedy();
        cp::need(greedy_sampler, "greedy sampler allocation failed", CITLALI_OUT_OF_MEMORY);
        m->api.llama_sampler_chain_add(d->sampler, greedy_sampler);
        d->context = m->api.llama_init_from_model(m->model, params);
        cp::need(d->context, "libllama context creation failed", CITLALI_INTERNAL_ERROR);
        m->decoders.insert(d.get());
        *out = reinterpret_cast<CitlaliDecoder>(d.release());
    });
}
CitlaliStatus CITLALI_CALL reset(CitlaliInstance h, CitlaliDecoder handle, CitlaliErrorV1 *e) {
    return cp::guard(e, [&] {
        auto m = model(h); auto d = decoder(m, handle);
        m->api.llama_memory_clear(m->api.llama_get_memory(d->context), true);
        m->api.llama_sampler_reset(d->sampler);
        d->used = 0; d->logits = false;
    });
}
CitlaliStatus CITLALI_CALL decode(CitlaliInstance h, CitlaliDecoder handle, const int32_t *tokens,
                                 uint64_t count, CitlaliErrorV1 *e) {
    return cp::guard(e, [&] {
        auto m = model(h); auto d = decoder(m, handle);
        cp::need(tokens && count && count <= m->api.llama_n_batch(d->context), "invalid decode batch");
        cp::need(d->used + count <= m->api.llama_n_ctx(d->context), "context capacity exceeded",
                 CITLALI_UNSUPPORTED);
        for (uint64_t i = 0; i < count; ++i) token(m, tokens[i]);
        // Copy because llama_batch_get_one accepts mutable storage.
        std::array<int32_t, 512> copy{};
        cp::need(count <= copy.size(), "decode batch exceeds plugin budget");
        std::copy(tokens, tokens + count, copy.begin());
        auto batch = m->api.llama_batch_get_one(copy.data(), static_cast<int32_t>(count));
        d->logits = false;
        cp::need(m->api.llama_decode(d->context, batch) == 0, "libllama decode failed; reset required",
                 CITLALI_INTERNAL_ERROR);
        d->used += count; d->logits = true;
    });
}
CitlaliStatus CITLALI_CALL greedy(CitlaliInstance h, CitlaliDecoder handle, int32_t *out,
                                 CitlaliErrorV1 *e) {
    return cp::guard(e, [&] {
        auto m = model(h); auto d = decoder(m, handle);
        cp::need(out && d->logits, "decode required before sampling");
        *out = m->api.llama_sampler_sample(d->sampler, d->context, -1);
        d->logits = false;
    });
}
CitlaliStatus CITLALI_CALL release(CitlaliInstance h, CitlaliDecoder handle, CitlaliErrorV1 *e) {
    return cp::guard(e, [&] {
        auto m = model(h); auto d = decoder(m, handle);
        m->decoders.erase(d); delete d;
    });
}
CitlaliStatus CITLALI_CALL create(const CitlaliHostV1 *host, CitlaliStringView config,
                                 CitlaliInstance *out, CitlaliErrorV1 *e) {
    if (out) *out = nullptr;
    return cp::guard(e, [&] {
        cp::need(out && host && host->struct_size >= sizeof(*host) && host->version == 1, "invalid host");
        auto c = toml::parse(cp::string(config));
        for (auto &&[key, value] : c)
            cp::need(key.str() == "model",
                     "unknown model config field");
        auto path = c["model"].value<std::string>();
        cp::need(path && !path->empty(), "model path required");
        auto file = std::filesystem::u8path(*path);
        if (file.is_relative()) file = std::filesystem::u8path(cp::string(host->deployment_path)) / file;
        cp::need(std::filesystem::is_regular_file(file), "GGUF model file missing: " + file.u8string());
        auto bin = std::filesystem::u8path(cp::string(host->package_path)) / "bin";
        auto m = std::make_unique<Model>(bin);
        m->api.llama_log_set(quiet_log, nullptr);
        auto params = m->api.llama_model_default_params();
        params.n_gpu_layers = 0;
        params.use_mmap = true;
        static ggml_backend_dev_t cpu_only[] = {nullptr};
        params.devices = cpu_only;
        params.split_mode = LLAMA_SPLIT_MODE_LAYER;
        params.progress_callback = [](float, void *) { return true; };
        m->model = m->api.llama_model_load_from_file(file.u8string().c_str(), params);
        cp::need(m->model, "GGUF loading failed", CITLALI_INTERNAL_ERROR);
        m->vocab = m->api.llama_model_get_vocab(m->model);
        cp::need(m->vocab, "GGUF missing vocabulary");
        char arch[128]{};
        int n = m->api.llama_model_meta_val_str(m->model, "general.architecture", arch, sizeof(arch));
        cp::need(n > 0 && n < static_cast<int>(sizeof(arch)), "invalid model architecture");
        m->architecture.assign(arch, n);
        cp::log(*host, "loaded " + m->architecture + " GGUF using CPU-only libllama; gpu_layers=0");
        *out = reinterpret_cast<CitlaliInstance>(m.release());
    });
}
CitlaliStatus CITLALI_CALL destroy(CitlaliInstance h, CitlaliErrorV1 *e) {
    return cp::guard(e, [&] { auto m = model(h); cp::need(m->decoders.empty(), "release decoders first", CITLALI_BUSY); delete m; });
}
CitlaliStatus CITLALI_CALL format_chat(CitlaliInstance h, CitlaliStringView input, char *out,
                                      uint64_t capacity, uint64_t *required, CitlaliErrorV1 *e) {
    return cp::guard(e, [&] {
        auto m = model(h);
        cp::need(required && capacity <= INT_MAX && (!capacity || out), "invalid chat buffer");
        auto prompt = cp::string(input);
        cp::need(prompt.size() <= 1024 * 1024 && cp::valid_utf8(prompt) && prompt.find('\0') == std::string::npos,
                 "prompt must be bounded UTF-8 without NUL");
        auto tmpl = m->api.llama_model_chat_template(m->model, nullptr);
        cp::need(tmpl && *tmpl, "GGUF chat template missing", CITLALI_UNSUPPORTED);
        llama_chat_message message{"user", prompt.c_str()};
        auto n = m->api.llama_chat_apply_template(tmpl, &message, 1, true, nullptr, 0);
        cp::need(n > 0 && n <= 4 * 1024 * 1024, "unsupported or oversized GGUF chat template", CITLALI_UNSUPPORTED);
        *required = static_cast<uint64_t>(n);
        if (!out && !capacity) return;
        cp::need(capacity >= *required, "chat capacity insufficient", CITLALI_UNSUPPORTED);
        cp::need(m->api.llama_chat_apply_template(tmpl, &message, 1, true, out, static_cast<int32_t>(capacity)) == n,
                 "chat formatting failed", CITLALI_INTERNAL_ERROR);
    });
}
const CitlaliChatFormatV1 chat_api{sizeof(chat_api), 1, format_chat};
const CitlaliGgufV1 vocab_api{sizeof(vocab_api), 1, architecture, tokenize, piece, is_end};
const CitlaliDecodeV1 decode_api{sizeof(decode_api), 1, prepare, reset, decode, greedy, release};
CitlaliStatus CITLALI_CALL query(CitlaliStringView id, const CitlaliInterfaceV1 **out, CitlaliErrorV1 *e) {
    if (out) *out = nullptr;
    return cp::guard(e, [&] {
        cp::need(out, "null query output");
        auto s = cp::string(id);
        static const auto v = cp::interface(CITLALI_GGUF_PROTOCOL, &vocab_api);
        static const auto d = cp::interface(CITLALI_DECODE_PROTOCOL, &decode_api);
        static const auto c = cp::interface(CITLALI_CHAT_FORMAT_PROTOCOL, &chat_api);
        if (s == CITLALI_CHAT_FORMAT_PROTOCOL) *out = &c;
        else if (s == CITLALI_GGUF_PROTOCOL) *out = &v;
        else if (s == CITLALI_DECODE_PROTOCOL) *out = &d;
        else throw cp::Failure(CITLALI_UNSUPPORTED, "unsupported model interface");
    });
}
const CitlaliStringView protocols[]{cp::view(CITLALI_GGUF_PROTOCOL), cp::view(CITLALI_DECODE_PROTOCOL), cp::view(CITLALI_CHAT_FORMAT_PROTOCOL)};
const CitlaliPluginApiV1 api{sizeof(api), CITLALI_NATIVE_ABI, cp::view(CITLALI_MODEL_PLUGIN_ID),
                            cp::view("0.1.0"), cp::view("model_load"), protocols, 3, query, create, destroy};
}
extern "C" CITLALI_EXPORT CitlaliStatus CITLALI_CALL citlali_plugin_entry(
    uint32_t v, const CitlaliPluginApiV1 **out, CitlaliErrorV1 *e) {
    if (out) *out = nullptr;
    return cp::guard(e, [&] { cp::need(out && v == CITLALI_NATIVE_ABI, "incompatible native ABI", CITLALI_INCOMPATIBLE_ABI); *out = &api; });
}
