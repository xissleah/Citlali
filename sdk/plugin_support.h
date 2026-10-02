#pragma once
// Private C++ authoring convenience. Never referenced by a public protocol header.
#include "abi.h"
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
namespace cp {
inline bool valid_utf8(const std::string &s) {
    for (size_t i = 0; i < s.size();) {
        auto lead = static_cast<unsigned char>(s[i++]);
        if (lead < 0x80)
            continue;
        uint32_t code = 0, minimum = 0;
        size_t remaining = 0;
        if (lead >= 0xc2 && lead <= 0xdf) {
            code = lead & 0x1f;
            remaining = 1;
            minimum = 0x80;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            code = lead & 0x0f;
            remaining = 2;
            minimum = 0x800;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            code = lead & 7;
            remaining = 3;
            minimum = 0x10000;
        } else
            return false;
        if (i + remaining > s.size())
            return false;
        while (remaining--) {
            auto c = static_cast<unsigned char>(s[i++]);
            if ((c & 0xc0) != 0x80)
                return false;
            code = (code << 6) | (c & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
            return false;
    }
    return true;
}
inline CitlaliStringView view(const std::string &s) {
    return {s.data(), static_cast<uint64_t>(s.size())};
}
inline CitlaliStringView view(const char *s) {
    return {s, static_cast<uint64_t>(std::strlen(s))};
}
inline std::string string(CitlaliStringView v) {
    if (v.length && !v.data)
        throw std::invalid_argument("null nonempty string");
    return v.length ? std::string(v.data, static_cast<size_t>(v.length)) : std::string();
}
struct Failure : std::runtime_error {
    CitlaliStatus code;
    Failure(CitlaliStatus c, std::string m) : std::runtime_error(std::move(m)), code(c) {}
};
inline void need(bool b, const std::string &m, CitlaliStatus c = CITLALI_INVALID_ARGUMENT) {
    if (!b)
        throw Failure(c, m);
}
inline void CITLALI_CALL release_error(void *p) {
    delete static_cast<std::string *>(p);
}
inline CitlaliStatus error(CitlaliErrorV1 *e, CitlaliStatus code, const char *text) noexcept {
    if (e && e->struct_size >= sizeof(*e)) {
        e->code = code;
        try {
            auto p = new std::string(text);
            e->message = view(*p);
            e->owner = p;
            e->release = release_error;
        } catch (...) {
            e->message = view("error allocation failed");
            e->owner = nullptr;
            e->release = nullptr;
        }
    }
    return code;
}
template <class F> CitlaliStatus guard(CitlaliErrorV1 *e, F &&f) noexcept {
    try {
        f();
        return CITLALI_OK;
    } catch (const Failure &x) {
        return error(e, x.code, x.what());
    } catch (const std::bad_alloc &) {
        return error(e, CITLALI_OUT_OF_MEMORY, "allocation failed");
    } catch (const std::exception &x) {
        return error(e, CITLALI_INTERNAL_ERROR, x.what());
    } catch (...) {
        return error(e, CITLALI_INTERNAL_ERROR, "unknown native exception");
    }
}
inline CitlaliErrorV1 err() {
    CitlaliErrorV1 e{};
    e.struct_size = sizeof(e);
    return e;
}
inline void check(CitlaliStatus status, CitlaliErrorV1 &e) {
    std::string message;
    try {
        message = string(e.message);
    } catch (...) {
        if (e.release)
            e.release(e.owner);
        throw;
    }
    if (e.release)
        e.release(e.owner);
    e = err();
    if (status != CITLALI_OK)
        throw Failure(status, message.empty() ? "dependency call failed" : message);
}
inline void log(const CitlaliHostV1 &h, const std::string &s) {
    h.log(h.context, 1, view(s));
}
inline CitlaliInterfaceV1 interface(const char *id, const void *functions, uint32_t version = 1) {
    return {sizeof(CitlaliInterfaceV1), version, view(id), functions};
}
// Only the envelope/prefix is generic. Consumers check concrete table size and callbacks.
inline const void *functions(const CitlaliInterfaceV1 *d, const std::string &id) {
    need(d && d->struct_size >= sizeof(*d), "invalid interface descriptor", CITLALI_INCOMPATIBLE_ABI);
    need(string(d->protocol_id) == id, "interface protocol ID mismatch", CITLALI_INCOMPATIBLE_ABI);
    need(d->protocol_version > 0 && d->functions, "invalid interface version/functions",
         CITLALI_INCOMPATIBLE_ABI);
    auto suffix = id.rfind("-v");
    if (suffix != std::string::npos) {
        auto digits = id.substr(suffix + 2);
        need(!digits.empty() && digits.find_first_not_of("0123456789") == std::string::npos &&
                 digits == std::to_string(d->protocol_version),
             "interface protocol version mismatch", CITLALI_INCOMPATIBLE_ABI);
    }
    CitlaliTableV1 head{};
    std::memcpy(&head.struct_size, d->functions, sizeof(head.struct_size));
    need(head.struct_size >= sizeof(head), "incompatible protocol prefix", CITLALI_INCOMPATIBLE_ABI);
    std::memcpy(&head, d->functions, sizeof(head));
    need(head.struct_size >= sizeof(head) && head.version == d->protocol_version,
         "incompatible protocol prefix", CITLALI_INCOMPATIBLE_ABI);
    return d->functions;
}
template <class T>
const T *query(const CitlaliHostV1 &h, const char *slot, uint64_t index, const char *protocol,
               CitlaliInstance *instance = nullptr, std::string *identity = nullptr,
               uint32_t expected_version = 1) {
    const CitlaliInterfaceV1 *p = nullptr;
    auto e = err();
    check(h.query_dependency(h.context, view(slot), index, view(protocol), &p, &e), e);
    auto table = static_cast<const T *>(functions(p, protocol));
    need(table->struct_size >= sizeof(T) && table->version == expected_version, "incompatible protocol table",
         CITLALI_INCOMPATIBLE_ABI);
    if (instance || identity) {
        CitlaliInstance i = nullptr;
        CitlaliStringView id{};
        check(h.dependency(h.context, view(slot), index, &i, &id, &e), e);
        if (instance)
            *instance = i;
        if (identity)
            *identity = string(id);
    }
    return table;
}
} // namespace cp
