#include "platform/web_transport.h"

#include <cstdlib>

#include "core/json.h"
#include "core/strutil.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

EM_JS(int, cc_js_net_send, (unsigned to, int reliable, const uint8_t* data, int size), {
    if (!window.CCINet) return 0;
    var bytes = HEAPU8.slice(data, data + size);
    return window.CCINet.send(to >>> 0, reliable !== 0, bytes) ? 1 : 0;
});
EM_JS(void, cc_js_net_describe, (const char* json), {
    if (window.CCINet) window.CCINet.describe(JSON.parse(UTF8ToString(json)));
});
EM_JS(void, cc_js_net_kick, (unsigned id), {
    if (window.CCINet) window.CCINet.kick(id >>> 0);
});
EM_JS(void, cc_js_net_leave, (), {
    if (window.CCINet) window.CCINet.leave();
});
#endif

namespace cc {
namespace net {

namespace {
// The few fields of the page's JSON: "self", "host", "roster", "reason".
long long jsonNumber(const std::string& j, const char* key) {
    const std::string k = std::string("\"") + key + "\":";
    const size_t at = j.find(k);
    return at == std::string::npos ? 0 : std::atoll(j.c_str() + at + k.size());
}
std::vector<PlayerId> jsonIds(const std::string& j, const char* key) {
    std::vector<PlayerId> out;
    const std::string k = std::string("\"") + key + "\":[";
    size_t at = j.find(k);
    if (at == std::string::npos) return out;
    at += k.size();
    while (at < j.size() && j[at] != ']') {
        char* end = nullptr;
        const long long v = std::strtoll(j.c_str() + at, &end, 10);
        if (end == j.c_str() + at) break;
        out.push_back(PlayerId(v));
        at = size_t(end - j.c_str());
        if (at < j.size() && j[at] == ',') ++at;
    }
    return out;
}
std::string jsonString(const std::string& j, const char* key) {
    const std::string k = std::string("\"") + key + "\":\"";
    size_t at = j.find(k);
    if (at == std::string::npos) return std::string();
    at += k.size();
    std::string out;
    while (at < j.size() && j[at] != '"') {
        if (j[at] == '\\' && at + 1 < j.size()) ++at;
        out += j[at++];
    }
    return out;
}
}  // namespace

namespace {
WebTransport* g_current = nullptr;
}

WebTransport::WebTransport() { g_current = this; }
WebTransport::~WebTransport() {
    if (g_current == this) g_current = nullptr;
}
WebTransport* WebTransport::current() { return g_current; }

bool WebTransport::send(PlayerId to, bool reliable, const uint8_t* data, size_t size) {
#ifdef __EMSCRIPTEN__
    if (!self_) return false;
    return cc_js_net_send(to, reliable ? 1 : 0, data, int(size)) != 0;
#else
    (void)to, (void)reliable, (void)data, (void)size;
    return false;
#endif
}

bool WebTransport::poll(TransportEvent& ev) {
    if (queue_.empty()) return false;
    ev = std::move(queue_.front());
    queue_.pop_front();
    return true;
}

void WebTransport::describe(const SessionDesc& d) {
#ifdef __EMSCRIPTEN__
    JsonWriter j;
    j.beginObject();
    j.field("name", d.name);
    j.field("password", d.password);
    j.field("maxPlayers", d.maxPlayers);
    j.field("sealed", d.sealed);
    j.field("city", d.city);
    j.field("mode", d.mode);
    j.field("version", d.version);
    j.endObject();
    cc_js_net_describe(j.str().c_str());
#else
    (void)d;
#endif
}

void WebTransport::kick(PlayerId id) {
#ifdef __EMSCRIPTEN__
    cc_js_net_kick(id);
#else
    (void)id;
#endif
}

void WebTransport::leave() {
#ifdef __EMSCRIPTEN__
    cc_js_net_leave();
#endif
    self_ = 0;
}

void WebTransport::push(int kind, PlayerId peer, bool reliable, const uint8_t* data, size_t size, const std::string& json) {
    TransportEvent ev;
    switch (kind) {
        case 0:
            ev.kind = TransportEvent::Opened;
            ev.self = PlayerId(jsonNumber(json, "self"));
            ev.host = PlayerId(jsonNumber(json, "host"));
            ev.roster = jsonIds(json, "roster");
            self_ = ev.self;
            break;
        case 1: ev.kind = TransportEvent::PeerJoined; break;
        case 2: ev.kind = TransportEvent::PeerLeft; break;
        case 3:
            ev.kind = TransportEvent::HostChanged;
            ev.host = PlayerId(jsonNumber(json, "host"));
            break;
        case 4: ev.kind = TransportEvent::Message; break;
        default:
            ev.kind = TransportEvent::Closed;
            ev.reason = jsonString(json, "reason");
            self_ = 0;
            break;
    }
    ev.peer = peer;
    ev.reliable = reliable;
    if (data && size) ev.data.assign(data, data + size);
    queue_.push_back(std::move(ev));
}

}  // namespace net
}  // namespace cc
