#include "platform/web_net_link.h"

#include "core/json.h"
#include "core/json_value.h"
#include "platform/web_transport.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

EM_JS(void, cc_js_link_host, (const char* server, const char* desc), {
    if (!window.CCINet) return;
    window.CCINet.hostSession(UTF8ToString(server), JSON.parse(UTF8ToString(desc))).catch(function (e) {
        Module.ccall('cc_netlink_error', null, ['string'], [e && e.message ? e.message : String(e)]);
    });
});
EM_JS(void, cc_js_link_join, (const char* server, const char* code, const char* pass, const char* version), {
    if (!window.CCINet) return;
    window.CCINet.joinSession(UTF8ToString(server), UTF8ToString(code), UTF8ToString(pass), UTF8ToString(version)).catch(function (e) {
        Module.ccall('cc_netlink_error', null, ['string'], [e && e.message ? e.message : String(e)]);
    });
});
EM_JS(void, cc_js_link_list, (const char* server), {
    if (!window.CCINet) return;
    window.CCINet.listSessions(UTF8ToString(server)).catch(function (e) {
        Module.ccall('cc_netlink_error', null, ['string'], [e && e.message ? e.message : String(e)]);
    });
});
EM_JS(void, cc_js_link_leave, (), {
    if (window.CCINet) window.CCINet.leave();
});
EM_JS(int, cc_js_link_code, (char* buf, int n), {
    var c = (window.CCINet && window.CCINet.code) || "";
    stringToUTF8(c, buf, n);
    return c.length;
});
EM_JS(int, cc_js_link_invite, (const char* code, const char* server, char* buf, int n), {
    var u = new URL(location.href);
    u.searchParams.delete('join');
    u.searchParams.set('join', UTF8ToString(code));
    var net = UTF8ToString(server);
    if (net) u.searchParams.set('net', net);
    u.searchParams.set('play', "");
    var s = u.toString();
    stringToUTF8(s, buf, n);
    return s.length;
});

extern "C" {
EMSCRIPTEN_KEEPALIVE void cc_netlink_sessions(const char* json) { cc::net::WebNetLink::get().pushSessions(json ? json : "[]"); }
EMSCRIPTEN_KEEPALIVE void cc_netlink_error(const char* text) { cc::net::WebNetLink::get().pushError(text ? text : "network error"); }
EMSCRIPTEN_KEEPALIVE void cc_netlink_set_default(const char* url) { cc::net::WebNetLink::get().setDefault(url ? url : ""); }
}
#endif

namespace cc {
namespace net {

WebNetLink& WebNetLink::get() {
    static WebNetLink l;
    return l;
}


std::string WebNetLink::defaultServer() const { return default_.empty() ? std::string("ws://127.0.0.1:8821/net") : default_; }

void WebNetLink::host(const std::string& server, const SessionDesc& d) {
#ifdef __EMSCRIPTEN__
    JsonWriter j;
    j.beginObject();
    j.field("name", d.name);
    j.field("password", d.password);
    j.field("maxPlayers", d.maxPlayers);
    j.field("version", d.version.empty() ? std::string("cci-1") : d.version);
    j.endObject();
    cc_js_link_host(server.c_str(), j.str().c_str());
#else
    (void)server;
    (void)d;
#endif
}

void WebNetLink::join(const std::string& server, const std::string& code, const std::string& password, const std::string& version) {
#ifdef __EMSCRIPTEN__
    cc_js_link_join(server.c_str(), code.c_str(), password.c_str(), version.c_str());
#else
    (void)server, (void)code, (void)password, (void)version;
#endif
}

void WebNetLink::list(const std::string& server) {
#ifdef __EMSCRIPTEN__
    cc_js_link_list(server.c_str());
#else
    (void)server;
#endif
}

void WebNetLink::leave() {
#ifdef __EMSCRIPTEN__
    cc_js_link_leave();
#endif
}

std::string WebNetLink::code() const {
#ifdef __EMSCRIPTEN__
    char buf[64] = {0};
    cc_js_link_code(buf, sizeof buf);
    return buf;
#else
    return std::string();
#endif
}

std::string WebNetLink::inviteUrl(const std::string& code, const std::string& server) const {
#ifdef __EMSCRIPTEN__
    std::string buf(4096, '\0');
    cc_js_link_invite(code.c_str(), server.c_str(), &buf[0], int(buf.size()));
    return std::string(buf.c_str());
#else
    (void)code;
    (void)server;
    return std::string();
#endif
}

void WebNetLink::pushSessions(const std::string& json) {
    sessions_.clear();
    const JValue v = JValue::parse(json);
    for (const JValue& s : v.items()) {
        SessionInfo i;
        i.code = s["code"].str();
        i.name = s["name"].str();
        i.city = s["city"].str();
        i.version = s["version"].str();
        i.mode = s["mode"].integer();
        i.players = s["players"].integer();
        i.max = s["max"].integer(8);
        i.sealed = s["sealed"].truthy();
        i.password = s["password"].truthy();
        sessions_.push_back(i);
    }
    haveSessions_ = true;
}

bool WebNetLink::takeSessions(std::vector<SessionInfo>& out) {
    if (!haveSessions_) return false;
    haveSessions_ = false;
    out = sessions_;
    return true;
}

bool WebNetLink::takeError(std::string& line) {
    if (errors_.empty()) return false;
    line = errors_.front();
    errors_.erase(errors_.begin());
    return true;
}

}  // namespace net
}  // namespace cc
