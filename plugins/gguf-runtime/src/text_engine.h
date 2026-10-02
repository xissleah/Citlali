#pragma once
#include "../include/text.h"
#include "plugin_support.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>
namespace demo {
struct Runtime;
struct Session {
    Runtime *owner;
    uint64_t retained_operations = 0;
};
struct Operation {
    Runtime *owner;
    Session *session;
    std::string prompt;
    uint32_t limit;
    CitlaliTextSinkV1 sink;
    void *sink_context;
    std::atomic<bool> cancelled{false};
    std::mutex mutex;
    std::condition_variable cv;
    uint32_t state = CITLALI_OPERATION_RUNNING;
    bool done = false;
    std::thread thread;
};
struct Runtime {
    CitlaliHostV1 host;
    std::string summary;
    std::mutex mutex;
    bool stopping = false;
    Operation *active = nullptr;
    std::set<Session *> sessions;
    std::set<Operation *> operations;
    std::function<void(Operation &)> compute;
    explicit Runtime(const CitlaliHostV1 &h) : host(h) {}
    virtual ~Runtime() {
        shutdown();
    }
    void shutdown() {
        {
            std::lock_guard<std::mutex> l(mutex);
            stopping = true;
            for (auto o : operations)
                o->cancelled = true;
        }
        // API calls from the owning frontend have ended before instance destroy.
        for (auto o : operations) {
            if (o->thread.joinable())
                o->thread.join();
            delete o;
        }
        operations.clear();
        for (auto s : sessions)
            delete s;
        sessions.clear();
        active = nullptr;
    }
};
inline Runtime *runtime(CitlaliInstance h) {
    cp::need(h, "null runtime instance");
    return reinterpret_cast<Runtime *>(h);
}
inline Operation *operation(Runtime *r, CitlaliOperation h) {
    auto o = reinterpret_cast<Operation *>(h);
    cp::need(r->operations.count(o), "operation not owned by runtime");
    return o;
}
inline bool emit(Operation &o, uint32_t kind, const std::string &text, uint64_t units = 0) {
    CitlaliTextEventV1 event{sizeof(event), kind, cp::view(text), units};
    return o.sink(o.sink_context, &event) != 0;
}
inline bool chunk(Operation &o, const std::string &text, uint64_t units = 0) {
    if (o.cancelled)
        return false;
    if (emit(o, CITLALI_EVENT_TEXT, text, units))
        o.cancelled = true;
    return !o.cancelled;
}
inline void worker(Operation *o) noexcept {
    uint32_t final = CITLALI_OPERATION_COMPLETED;
    std::string message;
    try {
        o->owner->compute(*o);
        if (o->cancelled)
            final = CITLALI_OPERATION_CANCELLED;
    } catch (const std::exception &e) {
        final = CITLALI_OPERATION_FAILED;
        message = e.what();
    } catch (...) {
        final = CITLALI_OPERATION_FAILED;
        message = "unhandled task failure";
    }
    // Cancellation may race the last safe point; completion wins after final is chosen.
    try {
        emit(*o,
             final == CITLALI_OPERATION_COMPLETED
                 ? CITLALI_EVENT_COMPLETED
                 : final == CITLALI_OPERATION_CANCELLED ? CITLALI_EVENT_CANCELLED : CITLALI_EVENT_FAILED,
             message);
    } catch (...) {
        final = CITLALI_OPERATION_FAILED;
    }
    {
        std::lock_guard<std::mutex> lock(o->owner->mutex);
        o->owner->active = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(o->mutex);
        o->state = final;
        o->done = true;
    }
    o->cv.notify_all();
}
inline CitlaliStatus CITLALI_CALL create_session(CitlaliInstance h, CitlaliSession *out, CitlaliErrorV1 *e) {
    if (out)
        *out = nullptr;
    return cp::guard(e, [&] {
        cp::need(out, "null session output");
        auto r = runtime(h);
        std::lock_guard<std::mutex> lock(r->mutex);
        cp::need(!r->stopping, "runtime stopping", CITLALI_CANCELLED);
        auto s = std::make_unique<Session>();
        s->owner = r;
        r->sessions.insert(s.get());
        *out = reinterpret_cast<CitlaliSession>(s.release());
    });
}
inline CitlaliStatus CITLALI_CALL destroy_session(CitlaliInstance h, CitlaliSession handle,
                                                  CitlaliErrorV1 *e) {
    return cp::guard(e, [&] {
        auto r = runtime(h);
        std::lock_guard<std::mutex> lock(r->mutex);
        auto s = reinterpret_cast<Session *>(handle);
        cp::need(r->sessions.count(s), "session not owned by runtime");
        cp::need(!s->retained_operations, "release session operations first", CITLALI_BUSY);
        r->sessions.erase(s);
        delete s;
    });
}
inline CitlaliStatus CITLALI_CALL submit(CitlaliInstance h, CitlaliSession handle,
                                         const CitlaliTextRequestV1 *request, CitlaliTextSinkV1 sink,
                                         void *ctx, CitlaliOperation *out, CitlaliErrorV1 *e) {
    if (out)
        *out = nullptr;
    return cp::guard(e, [&] {
        cp::need(out && request && request->struct_size >= sizeof(*request) && sink, "invalid text request");
        cp::need(request->max_tokens > 0 && request->max_tokens <= 4096, "max_tokens must be 1..4096");
        cp::need(request->prompt.length <= 1024 * 1024, "prompt exceeds 1 MiB");
        auto r = runtime(h);
        std::lock_guard<std::mutex> lock(r->mutex);
        auto s = reinterpret_cast<Session *>(handle);
        cp::need(r->sessions.count(s), "session not owned by runtime");
        cp::need(!r->stopping, "runtime stopping", CITLALI_CANCELLED);
        cp::need(!r->active, "runtime already executing a task", CITLALI_BUSY);
        auto o = std::make_unique<Operation>();
        o->owner = r;
        o->session = s;
        o->prompt = cp::string(request->prompt);
        cp::need(cp::valid_utf8(o->prompt), "prompt must be valid UTF-8");
        o->limit = request->max_tokens;
        o->sink = sink;
        o->sink_context = ctx;
        r->operations.insert(o.get());
        r->active = o.get();
        ++s->retained_operations;
        try {
            o->thread = std::thread(worker, o.get());
        } catch (...) {
            r->active = nullptr;
            r->operations.erase(o.get());
            --s->retained_operations;
            throw;
        }
        *out = reinterpret_cast<CitlaliOperation>(o.release());
    });
}
inline CitlaliStatus CITLALI_CALL wait(CitlaliInstance h, CitlaliOperation handle, uint64_t timeout,
                                       uint32_t *state, CitlaliErrorV1 *e) {
    return cp::guard(e, [&] {
        cp::need(state, "null operation state");
        auto r = runtime(h);
        Operation *o;
        {
            std::lock_guard<std::mutex> lock(r->mutex);
            o = operation(r, handle);
        }
        std::unique_lock<std::mutex> lock(o->mutex);
        if (timeout == UINT64_MAX)
            o->cv.wait(lock, [&] { return o->done; });
        else {
            cp::need(timeout <= 86400000, "timeout exceeds one day");
            if (!o->cv.wait_for(lock, std::chrono::milliseconds(timeout), [&] { return o->done; })) {
                *state = CITLALI_OPERATION_RUNNING;
                throw cp::Failure(CITLALI_TIMEOUT, "task still running");
            }
        }
        *state = o->state;
    });
}
inline CitlaliStatus CITLALI_CALL cancel(CitlaliInstance h, CitlaliOperation handle, CitlaliErrorV1 *e) {
    return cp::guard(e, [&] {
        auto r = runtime(h);
        std::lock_guard<std::mutex> lock(r->mutex);
        operation(r, handle)->cancelled = true;
    });
}
inline CitlaliStatus CITLALI_CALL release_operation(CitlaliInstance h, CitlaliOperation handle,
                                                    CitlaliErrorV1 *e) {
    return cp::guard(e, [&] {
        auto r = runtime(h);
        Operation *o;
        {
            std::lock_guard<std::mutex> lock(r->mutex);
            o = operation(r, handle);
            std::lock_guard<std::mutex> ol(o->mutex);
            cp::need(o->done, "operation still running", CITLALI_BUSY);
            r->operations.erase(o);
            --o->session->retained_operations;
        }
        if (o->thread.joinable())
            o->thread.join();
        delete o;
    });
}
inline CitlaliStatus CITLALI_CALL request_stop(CitlaliInstance h, CitlaliErrorV1 *e) {
    return cp::guard(e, [&] {
        auto r = runtime(h);
        std::lock_guard<std::mutex> lock(r->mutex);
        r->stopping = true;
        for (auto o : r->operations)
            o->cancelled = true;
    });
}
inline CitlaliStatus CITLALI_CALL plan_summary(CitlaliInstance h, CitlaliStringView *out, CitlaliErrorV1 *e) {
    return cp::guard(e, [&] {
        cp::need(out, "null summary output");
        *out = cp::view(runtime(h)->summary);
    });
}
inline CitlaliTextV1 text_api = {sizeof(CitlaliTextV1),
                                 1,
                                 create_session,
                                 destroy_session,
                                 submit,
                                 wait,
                                 cancel,
                                 release_operation,
                                 request_stop,
                                 plan_summary};
} // namespace demo
