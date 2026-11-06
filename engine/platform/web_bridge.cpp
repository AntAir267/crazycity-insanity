#include "platform/web_bridge.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// Copies [offset, offset + len) of archive `id` into wasm memory at dst. Returns 1 on success.
// A lazily read archive (window.CCI.archives[id].lazy, the audio archives) has only the ranges
// fetched so far.
EM_JS(int, cc_js_archive_read, (int id, double offset, int len, void* dst), {
    var a = window.CCI && window.CCI.archives[id];
    if (!a) return 0;
    if (offset < 0 || offset + len > a.byteLength) return 0;
    if (a.lazy) {
        var v = a.read(offset, len);
        if (!v) return 0;
        HEAPU8.set(v, dst);
        return 1;
    }
    HEAPU8.set(new Uint8Array(a, offset, len), dst);
    return 1;
});

EM_JS(int, cc_js_archive_lazy, (int id), {
    var a = window.CCI && window.CCI.archives[id];
    return a && a.lazy ? 1 : 0;
});
EM_JS(int, cc_js_archive_has, (int id, double offset, int len), {
    var a = window.CCI && window.CCI.archives[id];
    return a && a.lazy && a.has(offset, len) ? 1 : 0;
});
EM_JS(void, cc_js_archive_fetch, (int id, double offset, int len), {
    var a = window.CCI && window.CCI.archives[id];
    if (a && a.lazy) a.fetch(offset, len);
});
EM_JS(void, cc_js_archive_forget, (int id, double offset, int len), {
    var a = window.CCI && window.CCI.archives[id];
    if (a && a.lazy) a.forget(offset, len);
});

EM_JS(void, cc_js_log, (int level, const char* kind, const char* msg), {
    if (window.CCI && window.CCI.onLog) window.CCI.onLog(level, UTF8ToString(kind), UTF8ToString(msg));
});

EM_JS(void, cc_js_readout, (const char* text), {
    if (window.CCI && window.CCI.onReadout) window.CCI.onReadout(UTF8ToString(text));
});

EM_JS(void, cc_js_event, (const char* name, const char* json), {
    if (window.CCI && window.CCI.onEvent) window.CCI.onEvent(UTF8ToString(name), UTF8ToString(json));
});
#endif

#include <set>

namespace {
std::function<void(const std::string&, const std::string&)> g_sink;
std::set<std::string> g_subscribed;
}  // namespace

#ifdef __EMSCRIPTEN__
extern "C" EMSCRIPTEN_KEEPALIVE void cc_subscribe_event(const char* name, int on) {
    if (!name) return;
    if (on) g_subscribed.insert(name);
    else g_subscribed.erase(name);
}
#endif

namespace cc {

namespace {
#ifndef __EMSCRIPTEN__
NativePageSinks g_sinks;
#endif

#ifdef __EMSCRIPTEN__
class JsArchiveSource : public ByteSource {
public:
    JsArchiveSource(int id, double size) : id_(id), size_(uint64_t(size)), lazy_(cc_js_archive_lazy(id) != 0) {}
    uint64_t size() const override { return size_; }
    bool read(uint64_t offset, void* dst, size_t len) override {
        if (offset > size_ || len > size_ - offset) return false;
        if (len == 0) return true;
        return cc_js_archive_read(id_, double(offset), int(len), dst) != 0;
    }
    bool lazy() const override { return lazy_; }
    bool has(uint64_t offset, size_t len) override {
        return !lazy_ || len == 0 || cc_js_archive_has(id_, double(offset), int(len)) != 0;
    }
    void fetch(uint64_t offset, size_t len) override {
        if (lazy_ && len > 0) cc_js_archive_fetch(id_, double(offset), int(len));
    }
    void forget(uint64_t offset, size_t len) override {
        if (lazy_ && len > 0) cc_js_archive_forget(id_, double(offset), int(len));
    }

private:
    int id_;
    uint64_t size_;
    bool lazy_;
};
#endif
}  // namespace

std::shared_ptr<ByteSource> makeJsArchiveSource(int jsId, double size) {
#ifdef __EMSCRIPTEN__
    return std::make_shared<JsArchiveSource>(jsId, size);
#else
    (void)jsId;
    (void)size;
    return nullptr;
#endif
}

void pageLog(int level, const std::string& kind, const std::string& msg) {
#ifdef __EMSCRIPTEN__
    cc_js_log(level, kind.c_str(), msg.c_str());
#else
    if (g_sinks.log) g_sinks.log(level, kind, msg);
#endif
}

void pageReadout(const std::string& text) {
#ifdef __EMSCRIPTEN__
    cc_js_readout(text.c_str());
#else
    if (g_sinks.readout) g_sinks.readout(text);
#endif
}

void pageEvent(const std::string& name, const std::string& json) {
    if (g_sink) g_sink(name, json);
#ifdef __EMSCRIPTEN__
    if (g_subscribed.count(name) || g_subscribed.count("*")) cc_js_event(name.c_str(), json.c_str());
#else
    if (g_sinks.event) g_sinks.event(name, json);
#endif
}

void setNativePageSinks(NativePageSinks sinks) {
#ifdef __EMSCRIPTEN__
    (void)sinks;
#else
    g_sinks = std::move(sinks);
#endif
}

void setEventSink(std::function<void(const std::string&, const std::string&)> sink) { g_sink = std::move(sink); }

}  // namespace cc
