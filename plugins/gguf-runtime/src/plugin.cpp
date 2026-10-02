#include "../protocol.h"
#include "../include/model.h"
#include "../include/tokenizer.h"
#include "text_engine.h"
#include "toml.hpp"
#include <algorithm>
#include <chrono>
#include <sstream>
namespace {
struct Runtime : demo::Runtime {
    CitlaliInstance model = nullptr, tokenizer = nullptr;
    const CitlaliDecodeV1 *decode = nullptr;
    const CitlaliChatTokenizerV1 *tokens = nullptr;
    CitlaliDecoder decoder = nullptr;
    uint32_t context_size = 2048;
    explicit Runtime(const CitlaliHostV1 &h) : demo::Runtime(h) {}
    ~Runtime() {
        // Join generation before freeing KV/sampler and borrowed dependencies.
        shutdown();
        if (decoder) {
            auto e = cp::err(); auto status = decode->release(model, decoder, &e);
            if (e.release) e.release(e.owner);
            if (status != CITLALI_OK) std::terminate();
        }
    }
    void generate(demo::Operation &o) {
        auto e = cp::err();
        uint64_t n = 0;
        cp::check(tokens->encode_chat(tokenizer, cp::view(o.prompt), nullptr, 0, &n, &e), e);
        cp::need(n && n + o.limit <= context_size, "prompt plus max_tokens exceeds context_size", CITLALI_UNSUPPORTED);
        std::vector<int32_t> input(n);
        cp::check(tokens->encode_chat(tokenizer, cp::view(o.prompt), input.data(), input.size(), &n, &e), e);
        cp::check(decode->reset(model, decoder, &e), e);
        auto start = std::chrono::steady_clock::now();
        // Bounded prefill chunks give cancellation a safe point between decode calls.
        for (size_t pos = 0; pos < input.size(); pos += 128) {
            if (o.cancelled) return;
            auto count = std::min<size_t>(128, input.size() - pos);
            cp::check(decode->decode(model, decoder, input.data() + pos, count, &e), e);
        }
        auto prefilled = std::chrono::steady_clock::now();
        std::string pending;
        // Reuse a bounded token buffer; most BPE pieces fit without a size query
        // or allocation. Keep the protocol's insufficient-capacity fallback.
        std::vector<char> piece_buffer(256);
        uint32_t generated = 0;
        for (; generated < o.limit && !o.cancelled;) {
            int32_t t = 0; uint32_t end = 0;
            cp::check(decode->greedy(model, decoder, &t, &e), e);
            cp::check(tokens->is_end(tokenizer, t, &end, &e), e);
            if (end) break;
            ++generated;
            uint64_t bytes = 0;
            auto status = tokens->piece(tokenizer, t, piece_buffer.data(), piece_buffer.size(), &bytes, &e);
            if (status == CITLALI_UNSUPPORTED && bytes > piece_buffer.size()) {
                if (e.release) e.release(e.owner);
                e = cp::err();
                cp::need(bytes <= 1024 * 1024, "token piece too large");
                piece_buffer.resize(bytes);
                status = tokens->piece(tokenizer, t, piece_buffer.data(), piece_buffer.size(), &bytes, &e);
            }
            cp::check(status, e);
            cp::need(bytes <= piece_buffer.size(), "piece provider exceeded capacity");
            pending.append(piece_buffer.data(), bytes);
            if (cp::valid_utf8(pending)) {
                if (!pending.empty() && !demo::chunk(o, pending, generated)) break;
                pending.clear();
            }
            cp::need(pending.size() <= 4096, "invalid UTF-8 token stream");
            if (generated < o.limit && !o.cancelled)
                cp::check(decode->decode(model, decoder, &t, 1, &e), e);
        }
        // A limit may cut a UTF-8 character across BPE tokens. Emit a valid replacement.
        if (!o.cancelled && !pending.empty()) demo::chunk(o, "\xef\xbf\xbd", generated);
        if (!o.cancelled) demo::chunk(o, "\n", generated);
        auto finished = std::chrono::steady_clock::now();
        double prefill = std::chrono::duration<double>(prefilled - start).count();
        double generation = std::chrono::duration<double>(finished - prefilled).count();
        std::ostringstream stats;
        stats << "prompt_tokens=" << input.size() << " generated_tokens=" << generated
              << " prefill_s=" << prefill << " generation_s=" << generation
              << " tokens_per_s=" << (generation > 0 ? generated / generation : 0)
              << (o.cancelled ? " cancelled" : "");
        cp::log(host, stats.str());
    }
};
uint32_t setting(const toml::table &c, const char *key, uint32_t fallback, uint32_t lo, uint32_t hi) {
    if (!c.contains(key)) return fallback;
    auto v = c[key].value<int64_t>();
    cp::need(c[key].is_integer() && v && *v >= lo && *v <= hi, std::string("invalid ") + key);
    return static_cast<uint32_t>(*v);
}
CitlaliStatus CITLALI_CALL create(const CitlaliHostV1 *host, CitlaliStringView config,
                                 CitlaliInstance *out, CitlaliErrorV1 *e) {
    if (out) *out = nullptr;
    return cp::guard(e, [&] {
        cp::need(out && host && host->struct_size >= sizeof(*host) && host->version == 1, "invalid host");
        auto c = toml::parse(cp::string(config));
        for (auto &&[key, value] : c)
            cp::need(key.str() == "context_size" || key.str() == "threads", "unknown GGUF runtime field");
        auto r = std::make_unique<Runtime>(*host);
        r->context_size = setting(c, "context_size", 2048, 128, 32768);
        uint32_t threads = setting(c, "threads", 4, 1, 256);
        std::string model_id, tokenizer_id;
        r->decode = cp::query<CitlaliDecodeV1>(*host, "model", 0, CITLALI_DECODE_PROTOCOL, &r->model, &model_id);
        r->tokens = cp::query<CitlaliChatTokenizerV1>(*host, "tokenizer", 0, CITLALI_CHAT_TOKENIZER_PROTOCOL,
                                                     &r->tokenizer, &tokenizer_id);
        cp::need(r->decode->prepare && r->decode->reset && r->decode->decode && r->decode->greedy && r->decode->release &&
                     r->tokens->model_identity && r->tokens->encode_chat && r->tokens->piece && r->tokens->is_end,
                 "incomplete GGUF interfaces", CITLALI_INCOMPATIBLE_ABI);
        auto error = cp::err(); CitlaliStringView identity{};
        cp::check(r->tokens->model_identity(r->tokenizer, &identity, &error), error);
        cp::need(cp::string(identity) == model_id, "tokenizer/model vocabulary binding mismatch");
        cp::check(r->decode->prepare(r->model, r->context_size, threads, &r->decoder, &error), error);
        r->summary = "Real GGUF CPU inference through Citlali native plugins\nmodel=" + model_id +
                     "\ntokenizer=" + tokenizer_id + "\nbackend=libllama C API (in-process; no CLI subprocess)" +
                     "\ncontext_size=" + std::to_string(r->context_size) +
                     "; CPU greedy; model chat template; prompt replaces history; one active request\n";
        auto raw = r.get(); r->compute = [raw](demo::Operation &o) { raw->generate(o); };
        *out = reinterpret_cast<CitlaliInstance>(r.release());
    });
}
CitlaliStatus CITLALI_CALL destroy(CitlaliInstance h, CitlaliErrorV1 *e) {
    return cp::guard(e, [&] { delete demo::runtime(h); });
}
CitlaliStatus CITLALI_CALL query(CitlaliStringView id, const CitlaliInterfaceV1 **out, CitlaliErrorV1 *e) {
    if (out) *out = nullptr;
    return cp::guard(e, [&] {
        cp::need(out, "null query output");
        cp::need(cp::string(id) == CITLALI_TEXT_PROTOCOL, "unsupported runtime interface", CITLALI_UNSUPPORTED);
        static const auto d = cp::interface(CITLALI_TEXT_PROTOCOL, &demo::text_api); *out = &d;
    });
}
const CitlaliStringView protocols[]{cp::view(CITLALI_TEXT_PROTOCOL)};
const CitlaliPluginApiV1 api{sizeof(api), CITLALI_NATIVE_ABI, cp::view("example.gguf-runtime"),
                            cp::view("0.1.0"), cp::view("runtime"), protocols, 1, query, create, destroy};
}
extern "C" CITLALI_EXPORT CitlaliStatus CITLALI_CALL citlali_plugin_entry(
    uint32_t v, const CitlaliPluginApiV1 **out, CitlaliErrorV1 *e) {
    if (out) *out = nullptr;
    return cp::guard(e, [&] { cp::need(out && v == CITLALI_NATIVE_ABI, "incompatible native ABI", CITLALI_INCOMPATIBLE_ABI); *out = &api; });
}
