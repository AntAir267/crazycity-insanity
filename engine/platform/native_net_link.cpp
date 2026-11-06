#include "platform/native_net_link.h"

#include <algorithm>

#include "app/app.h"
#include "core/log.h"
#include "core/strutil.h"
#include "native/files.h"
#include "platform/host_server.h"
#include "platform/native_transport.h"

namespace cc {
namespace net {

NativeNetLink::NativeNetLink(App& app, native::Settings* settings) : app_(app), settings_(settings) {}
NativeNetLink::~NativeNetLink() = default;

NativeTransport* NativeNetLink::transport() const { return app_.nativeTransport(); }

void NativeNetLink::save() {
    if (settings_) settings_->save();
}

void NativeNetLink::applySettings() {
    NativeNetConfig cfg = app_.netConfig();
    if (settings_) {
        cfg.server = serverUrl(settings_->get("net_server", "ws://127.0.0.1:8821/net"));
        cfg.direct = settings_->getBool("net_direct", true);
        cfg.link.port = uint16_t(std::clamp(settings_->getInt("net_udp_port", 0), 0, 65535));
        cfg.ws.caFile = settings_->get("net_ca_file");
        cfg.ws.verifyTls = settings_->getBool("net_verify_tls", true);
    }
    app_.setNetConfig(cfg);
}

std::string NativeNetLink::defaultServer() const {
    return settings_ ? serverUrl(settings_->get("net_server", "ws://127.0.0.1:8821/net")) : std::string("ws://127.0.0.1:8821/net");
}

void NativeNetLink::host(const std::string& server, const SessionDesc& desc) {
    NativeTransport* t = transport();
    if (!t) {
        errors_.push_back("No network session to host.");
        return;
    }
    std::string url;
    if (hostHere()) {
        std::string err;
        if (!startLocalServer(err)) {
            errors_.push_back("Cannot host on this computer: " + err);
            return;
        }
        url = localServerUrl();
        stopServerOnLeave_ = true;
    } else {
        url = serverUrl(server);
    }
    if (!t->connect(url)) {
        errors_.push_back("Not a server address: " + server);
        return;
    }
    lastFailure_.clear();
    t->host(desc);
}

void NativeNetLink::join(const std::string& server, const std::string& code, const std::string& password, const std::string& version) {
    NativeTransport* t = transport();
    if (!t) {
        errors_.push_back("No network session to join with.");
        return;
    }
    if (!t->connect(serverUrl(server))) {
        errors_.push_back("Not a server address: " + server);
        return;
    }
    lastFailure_.clear();
    t->join(code, password, version);
}

// The server's games and the LAN servers' games: the answer comes when the server answered and
// the LAN had a moment to answer, or after three seconds with what came.
void NativeNetLink::list(const std::string& server) {
    listed_.clear();
    listing_ = true;
    listStarted_ = app_.clock();
    lister_ = std::make_unique<NativeTransport>(app_.netConfig());
    if (lister_->connect(serverUrl(server))) lister_->requestList();
    if (!lan_) lan_ = std::make_unique<LanSearch>();
    if (!lan_->searching()) {
        std::string err;
        const int port = settings_ ? std::clamp(settings_->getInt("net_host_port", int(kDefaultNetPort)), 1, 65535) : int(kDefaultNetPort);
        if (!lan_->start(uint16_t(port), &err)) logInfo("network: no LAN search (%s)", err.c_str());
    }
}

bool NativeNetLink::takeSessions(std::vector<SessionInfo>& out) {
    if (!listing_) return false;
    const double now = app_.clock(), waited = now - listStarted_;
    bool fromServer = false;
    if (lister_) {
        lister_->service();
        std::vector<SessionInfo> got;
        if (lister_->takeSessionList(got)) {
            for (SessionInfo& s : got) listed_.push_back(std::move(s));
            fromServer = true;
            lister_->disconnect();
            lister_.reset();
        } else if (lister_->state() == NativeTransport::State::Failed) {
            lister_.reset();
        }
    }
    (void)fromServer;
    const bool serverDone = !lister_;
    if (!(serverDone && waited >= 1.5) && waited < 3.0) return false;
    // LAN servers' games (each with its own address), unless the asked server listed them.
    if (lan_) {
        for (const LanServer& ls : lan_->results())
            for (const SessionInfo& s : ls.sessions) {
                bool seen = false;
                for (const SessionInfo& x : listed_) seen = seen || x.code == s.code;
                if (seen) continue;
                SessionInfo c = s;
                c.server = ls.url;
                if (!ls.name.empty()) c.name += " (" + ls.name + ")";
                listed_.push_back(c);
            }
        lan_->stop();
    }
    if (lister_) {
        lister_->disconnect();
        lister_.reset();
    }
    listing_ = false;
    out = listed_;
    return true;
}

bool NativeNetLink::takeError(std::string& line) {
    if (errors_.empty()) return false;
    line = errors_.front();
    errors_.erase(errors_.begin());
    return true;
}

void NativeNetLink::service() {
    if (lister_) lister_->service();
    if (lan_ && lan_->searching()) lan_->service(app_.clock());
    if (NativeTransport* t = transport()) {
        if (t->state() == NativeTransport::State::Failed && !t->error().empty() && t->error() != lastFailure_) {
            lastFailure_ = t->error();
            errors_.push_back("Network: " + t->error());
        }
    }
    // The session server of a game hosted here stops once the leaving messages are out.
    if (stopIn_ > 0 && --stopIn_ == 0) stopLocalServer();
}

void NativeNetLink::leave() {
    if (NativeTransport* t = transport()) t->leave();
    if (stopServerOnLeave_) {
        stopServerOnLeave_ = false;
        stopIn_ = 30;
    }
}

std::string NativeNetLink::code() const {
    NativeTransport* t = transport();
    return t ? t->code() : std::string();
}

// Natively there is no page address: the code and where to join it.
std::string NativeNetLink::inviteUrl(const std::string& code, const std::string& server) const {
    std::string where = server;
    if (localServerRunning()) {
        const std::vector<std::string> u = localServerUrls();
        where.clear();
        for (const std::string& s : u)
            if (s.find("127.0.0.1") == std::string::npos) where = s;
        if (where.empty() && !u.empty()) where = u.front();
    }
    return "CrazyCity Insanity game " + code + " on the server " + where;
}

bool NativeNetLink::hostHere() const { return settings_ ? settings_->getBool("net_host_here", true) : true; }
void NativeNetLink::setHostHere(bool on) {
    if (!settings_) return;
    settings_->setBool("net_host_here", on);
    save();
}
bool NativeNetLink::lan() const { return settings_ ? settings_->getBool("net_host_lan", false) : false; }
void NativeNetLink::setLan(bool on) {
    if (!settings_) return;
    settings_->setBool("net_host_lan", on);
    save();
}

bool NativeNetLink::startLocalServer(std::string& error) {
    const int port = settings_ ? std::clamp(settings_->getInt("net_host_port", int(kDefaultNetPort)), 0, 65535) : int(kDefaultNetPort);
    if (app_.netStartLocalServer(uint16_t(port), lan(), &error)) return true;
    // The port is taken (another server here): any free one; friends then need the port.
    std::string err2;
    if (port != 0 && app_.netStartLocalServer(0, lan(), &err2)) {
        logInfo("network: port %d is taken (%s); the session server takes port %u", port, error.c_str(), unsigned(app_.netLocalServer()->port()));
        error.clear();
        return true;
    }
    return false;
}

void NativeNetLink::stopLocalServer() {
    app_.netStopLocalServer();
    applySettings();  // the configured server again for the next session
}

bool NativeNetLink::localServerRunning() const {
    HostServer* s = const_cast<App&>(app_).netLocalServer();
    return s && s->running();
}

std::string NativeNetLink::localServerUrl() const {
    HostServer* s = const_cast<App&>(app_).netLocalServer();
    return s ? "ws://127.0.0.1:" + std::to_string(s->port()) + "/net" : std::string();
}

std::vector<std::string> NativeNetLink::localServerUrls() const {
    HostServer* s = const_cast<App&>(app_).netLocalServer();
    return s ? s->urls() : std::vector<std::string>();
}

std::string NativeNetLink::linkTo(double playerId) const {
    NativeTransport* t = transport();
    if (!t) return std::string();
    for (const PeerLink& k : t->links()) {
        if (double(k.id) != playerId) continue;
        if (k.direct) return k.rtt >= 0 ? strf("direct, %.0f ms", k.rtt * 1000.0) : std::string("direct");
        return k.native ? "relayed" : "browser, relayed";
    }
    return std::string();
}

std::string NativeNetLink::statusLine() const {
    NativeTransport* t = transport();
    if (!t) return std::string();
    switch (t->state()) {
        case NativeTransport::State::Connecting: return "Connecting to " + t->serverUrl() + "…";
        case NativeTransport::State::Waiting: return "Waiting for the server at " + t->serverUrl() + "…";
        case NativeTransport::State::Failed: return "No connection: " + t->error();
        default: return std::string();
    }
}

void NativeNetLink::rememberServer(const std::string& typed) {
    if (!settings_ || trim(typed).empty()) return;
    settings_->set("net_server", serverUrl(trim(typed)));
    save();
    applySettings();
}

}  // namespace net
}  // namespace cc
