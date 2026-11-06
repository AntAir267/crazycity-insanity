#include "platform/native_transport.h"

#include <chrono>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "core/strutil.h"

namespace cc {
namespace net {

namespace {
constexpr uint8_t kRel = 1, kUnrel = 0;

std::string hex64(uint64_t v) {
    char b[17];
    std::snprintf(b, sizeof b, "%016llx", (unsigned long long)v);
    return b;
}
uint64_t unhex64(const std::string& s) {
    uint64_t v = 0;
    for (char c : s) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= uint64_t(c - '0');
        else if (c >= 'a' && c <= 'f') v |= uint64_t(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= uint64_t(c - 'A' + 10);
        else return 0;
    }
    return v;
}
void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i)));
}
uint32_t get32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

std::vector<std::string> stringList(const JsValue& v) {
    std::vector<std::string> out;
    for (const JsValue& x : v.items())
        if (x.isString()) out.push_back(x.str());
    return out;
}
bool hasCap(const std::vector<std::string>& caps, const char* c) {
    for (const std::string& x : caps)
        if (x == c) return true;
    return false;
}
}  // namespace

std::string serverUrl(const std::string& typedIn) {
    std::string t = trim(typedIn);
    if (t.empty()) return t;
    std::string scheme = "ws://";
    if (istartsWith(t, "ws://")) {
        t = t.substr(5);
    } else if (istartsWith(t, "wss://")) {
        scheme = "wss://";
        t = t.substr(6);
    } else if (istartsWith(t, "http://")) {
        t = t.substr(7);
    } else if (istartsWith(t, "https://")) {
        scheme = "wss://";
        t = t.substr(8);
    }
    const size_t slash = t.find('/');
    std::string hostport = slash == std::string::npos ? t : t.substr(0, slash);
    std::string path = slash == std::string::npos ? std::string() : t.substr(slash);
    if (path.empty() || path == "/") path = "/net";
    // A port unless one is given (an IPv6 address in brackets may carry one after the bracket).
    const size_t close = hostport.rfind(']');
    const size_t colon = hostport.rfind(':');
    const bool hasPort = colon != std::string::npos && (close == std::string::npos || colon > close) &&
                         (hostport[0] == '[' || hostport.find(':') == colon);
    if (!hasPort) {
        if (hostport.find(':') != std::string::npos && hostport[0] != '[') hostport = "[" + hostport + "]";  // a bare IPv6 address
        if (scheme == "ws://") hostport += ":" + std::to_string(kDefaultNetPort);
    }
    return scheme + hostport + path;
}

NativeTransport::NativeTransport(NativeNetConfig cfg) : cfg_(std::move(cfg)) {
    sock::init();
    clock_ = []() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    };
    if (cfg_.direct) {
        DirectLinks::Hooks h;
        h.reliable = [this](PlayerId from, uint32_t seq, const uint8_t* d, size_t n) {
            auto it = peers_.find(from);
            if (it == peers_.end()) return;
            ++it->second.recvDirect;
            deliverReliable(it->second, seq, d, n);
        };
        h.delivered = [this](PlayerId from) -> uint32_t {
            auto it = peers_.find(from);
            return it == peers_.end() ? 0 : it->second.recvNext - 1;
        };
        h.unreliable = [this](PlayerId from, const uint8_t* d, size_t n) {
            auto it = peers_.find(from);
            if (it == peers_.end()) return;
            ++it->second.recvDirect;
            TransportEvent ev;
            ev.kind = TransportEvent::Message;
            ev.peer = from;
            ev.reliable = false;
            ev.data.assign(d, d + n);
            push(std::move(ev));
        };
        h.toRelay = [this](PlayerId to, uint32_t seq, const std::vector<uint8_t>& data) {
            if (!peers_.count(to)) return;
            std::vector<uint8_t> f;
            f.push_back(kRel);
            put32(f, seq);
            f.insert(f.end(), data.begin(), data.end());
            relayFrame(to, true, f);
        };
        h.path = [this](PlayerId peer, bool up) {
            if (up) logInfo("network: direct path to player %u via %s", unsigned(peer), direct_.pathOf(peer).c_str());
            else logInfo("network: direct path to player %u lost (the relay carries on)", unsigned(peer));
        };
        h.pong = [this](PlayerId peer, uint32_t n) {
            if (n == 0 || n != syncN_) return;
            for (size_t i = 0; i < syncPeers_.size(); ++i)
                if (syncPeers_[i] == peer) {
                    syncPeers_.erase(syncPeers_.begin() + long(i));
                    break;
                }
        };
        std::string err;
        if (!direct_.open(0, cfg_.link, std::move(h), &err)) logWarn("network: no direct UDP path (%s); the relay only", err.c_str());
    }
}

NativeTransport::~NativeTransport() {
    if (self_ && ws_.isOpen()) leave();
    ws_.close();
    direct_.close();
}

double NativeTransport::now() const { return clock_(); }

const char* NativeTransport::stateName() const {
    switch (state_) {
        case State::Idle: return "idle";
        case State::Connecting: return "connecting";
        case State::Connected: return "connected";
        case State::Waiting: return "waiting";
        case State::InSession: return "in session";
        case State::Failed: return "failed";
    }
    return "?";
}

// ---- the server connection

bool NativeTransport::connect(const std::string& url) {
    const std::string u = url.empty() ? cfg_.server : url;
    vanished_ = false;
    if (u == url_ && (ws_.state() != ws::Client::Closed && ws_.state() != ws::Client::Idle)) return true;
    resetSession();
    serverProto_ = 0;
    serverUdp_ = 0;
    url_ = u;
    error_.clear();
    if (!ws_.open(u, cfg_.ws)) {
        state_ = State::Failed;
        error_ = ws_.error();
        return false;
    }
    state_ = State::Connecting;
    service();
    return state_ != State::Failed;
}

void NativeTransport::ensureConnected() {
    if (ws_.state() == ws::Client::Idle || ws_.state() == ws::Client::Closed) connect(url_.empty() ? cfg_.server : url_);
}

JsValue NativeTransport::capsValue() const {
    JsValue c = JsValue::array();
    c.push("native");
    if (cfg_.direct && direct_.isOpen()) c.push("udp");
    return c;
}

void NativeTransport::sendControl(const JsValue& v) {
    if (ws_.state() == ws::Client::Closed || ws_.state() == ws::Client::Idle) return;
    ws_.sendText(v.dump());
}

namespace {
JsValue descValue(const SessionDesc& d) {
    JsValue o = JsValue::object();
    o.set("name", d.name);
    o.set("password", d.password);
    o.set("maxPlayers", d.maxPlayers);
    o.set("sealed", d.sealed);
    o.set("city", d.city);
    o.set("mode", d.mode);
    o.set("version", d.version);
    return o;
}
}  // namespace

void NativeTransport::host(const SessionDesc& d) {
    ensureConnected();
    if (state_ == State::Failed) {
        TransportEvent ev;
        ev.kind = TransportEvent::Closed;
        ev.reason = "cannot reach the server: " + error_;
        push(std::move(ev));
        return;
    }
    JsValue m = JsValue::object();
    m.set("t", "host");
    m.set("desc", descValue(d));
    m.set("caps", capsValue());
    m.set("proto", kNetProtocol);
    sendControl(m);
    state_ = State::Waiting;
}

void NativeTransport::join(const std::string& code, const std::string& password, const std::string& version) {
    ensureConnected();
    if (state_ == State::Failed) {
        TransportEvent ev;
        ev.kind = TransportEvent::Closed;
        ev.reason = "cannot reach the server: " + error_;
        push(std::move(ev));
        return;
    }
    JsValue m = JsValue::object();
    m.set("t", "join");
    m.set("code", code);
    m.set("password", password);
    m.set("version", version);
    m.set("caps", capsValue());
    m.set("proto", kNetProtocol);
    sendControl(m);
    state_ = State::Waiting;
}

void NativeTransport::requestList() {
    ensureConnected();
    JsValue m = JsValue::object();
    m.set("t", "list");
    sendControl(m);
}

bool NativeTransport::takeSessionList(std::vector<SessionInfo>& out) {
    if (!haveList_) return false;
    out = list_;
    haveList_ = false;
    return true;
}

void NativeTransport::disconnect() {
    if (self_) leave();
    ws_.close();
    resetSession();
    state_ = State::Idle;
}

void NativeTransport::resetSession() {
    peers_.clear();
    direct_.removeAll();
    self_ = host_ = 0;
    code_.clear();
    syncPeers_.clear();
}

void NativeTransport::lostServer(const std::string& why) {
    if (self_) {
        TransportEvent ev;
        ev.kind = TransportEvent::Closed;
        ev.reason = "lost";
        push(std::move(ev));
    } else if (state_ == State::Waiting) {
        TransportEvent ev;
        ev.kind = TransportEvent::Closed;
        ev.reason = "cannot reach the server: " + why;
        push(std::move(ev));
    }
    resetSession();
    error_ = why.empty() ? "the connection to the server was lost" : why;
    state_ = State::Failed;
    syncServer_ = false;
    logInfo("network: %s", error_.c_str());
}

void NativeTransport::service() {
    if (vanished_) return;
    const double t = now();
    ws_.service(t);
    if (state_ == State::Connecting && ws_.isOpen()) state_ = State::Connected;
    ws::Decoder::Message m;
    while (!vanished_ && ws_.next(m)) {
        if (m.op == ws::Text) onText(std::string(m.data.begin(), m.data.end()));
        else onBinary(m.data.data(), m.data.size());
    }
    if (ws_.state() == ws::Client::Closed && state_ != State::Idle && state_ != State::Failed) lostServer(ws_.error());
    if (cfg_.direct) {
        direct_.service(t);
        if (direct_.takeCandidatesChanged())
            for (auto& kv : peers_)
                if (kv.second.signalled) signalDirect(kv.second);
    }
}

// ---- server messages

void NativeTransport::onText(const std::string& text) {
    JsValue m;
    if (!JsValue::parse(text, m) || !m.isObject()) return;
    const std::string& t = m["t"].str();
    if (t == "welcome") {
        serverProto_ = int(m["proto"].integer());
        serverUdp_ = uint16_t(m["udp"].integer());
        if (directWanted() && serverUdp_ != 0) {
            sock::Addr a = ws_.peer();
            a.setPort(serverUdp_);
            direct_.askReflexive(a, now());
        }
        return;
    }
    if (t == "pong") {
        if (uint32_t(m["n"].integer()) == syncN_) syncServer_ = false;
        return;
    }
    if (t == "sessions") {
        list_.clear();
        for (const JsValue& s : m["list"].items()) {
            SessionInfo i;
            i.code = s["code"].str();
            i.name = s["name"].str();
            i.city = s["city"].str();
            i.version = s["version"].str();
            i.players = int(s["players"].integer());
            i.max = int(s["max"].integer(8));
            i.mode = int(s["mode"].integer());
            i.sealed = s["sealed"].boolean();
            i.password = s["password"].boolean();
            list_.push_back(i);
        }
        haveList_ = true;
        return;
    }
    if (t == "opened") {
        self_ = PlayerId(m["self"].integer());
        host_ = PlayerId(m["host"].integer());
        code_ = m["code"].str();
        state_ = State::InSession;
        direct_.setSelf(self_);
        TransportEvent ev;
        ev.kind = TransportEvent::Opened;
        ev.self = self_;
        ev.host = host_;
        for (const JsValue& id : m["roster"].items()) {
            const PlayerId p = PlayerId(id.integer());
            ev.roster.push_back(p);
            if (p == self_) continue;
            addPeer(p, stringList(m["caps"][std::to_string(p)]));
        }
        push(std::move(ev));
        // The newcomer sends its direct-path details to every native player already there.
        for (auto& kv : peers_)
            if (kv.second.native && directWanted() && hasCap(kv.second.caps, "udp")) signalDirect(kv.second);
        return;
    }
    if (t == "joined") {
        const PlayerId p = PlayerId(m["peer"].integer());
        if (!self_ || p == self_) return;
        addPeer(p, stringList(m["caps"]));
        TransportEvent ev;
        ev.kind = TransportEvent::PeerJoined;
        ev.peer = p;
        push(std::move(ev));
        Peer& peer = peers_[p];
        if (peer.native && directWanted() && hasCap(peer.caps, "udp")) signalDirect(peer);
        return;
    }
    if (t == "left") {
        const PlayerId p = PlayerId(m["peer"].integer());
        if (!self_) return;
        dropPeer(p);
        TransportEvent ev;
        ev.kind = TransportEvent::PeerLeft;
        ev.peer = p;
        push(std::move(ev));
        return;
    }
    if (t == "host") {
        if (!self_) return;
        host_ = PlayerId(m["host"].integer());
        TransportEvent ev;
        ev.kind = TransportEvent::HostChanged;
        ev.host = host_;
        push(std::move(ev));
        return;
    }
    if (t == "closed") {
        TransportEvent ev;
        ev.kind = TransportEvent::Closed;
        ev.reason = m["reason"].jsString();
        push(std::move(ev));
        resetSession();
        state_ = ws_.isOpen() ? State::Connected : State::Failed;
        return;
    }
    if (t == "signal") {
        const PlayerId from = PlayerId(m["from"].integer());
        auto it = peers_.find(from);
        if (it == peers_.end()) return;
        const JsValue& udp = m["data"]["udp"];
        if (!udp.isObject() || !directWanted()) return;  // WebRTC offers (a browser) are not for us
        const uint64_t tok = unhex64(udp["tok"].str());
        if (!tok) return;
        it->second.native = true;
        if (!hasCap(it->second.caps, "udp")) it->second.caps.push_back("udp");
        direct_.remote(from, tok, stringList(udp["cands"]), now());
        if (!it->second.signalled) signalDirect(it->second);
        return;
    }
}

void NativeTransport::onBinary(const uint8_t* d, size_t n) {
    // Relayed: [u32 from][u8 flags][frame].
    if (n < 5 || !self_) return;
    onFrame(get32(d), d + 5, n - 5, false);
}

void NativeTransport::onFrame(PlayerId from, const uint8_t* d, size_t n, bool direct) {
    auto it = peers_.find(from);
    if (it == peers_.end() || n < 1) return;
    Peer& p = it->second;
    if (direct) ++p.recvDirect;
    else ++p.recvRelay;
    if (d[0] == kRel) {
        if (n < 5) return;
        deliverReliable(p, get32(d + 1), d + 5, n - 5);
    } else if (d[0] == kUnrel) {
        TransportEvent ev;
        ev.kind = TransportEvent::Message;
        ev.peer = from;
        ev.reliable = false;
        ev.data.assign(d + 1, d + n);
        push(std::move(ev));
    }
}

void NativeTransport::deliverReliable(Peer& p, uint32_t seq, const uint8_t* d, size_t n) {
    if (seq < p.recvNext || p.pending.count(seq)) return;  // a copy (it came both ways)
    if (p.pending.size() > 8192) return;                    // something is badly wrong; keep memory bounded
    p.pending[seq].assign(d, d + n);
    for (auto it = p.pending.find(p.recvNext); it != p.pending.end(); it = p.pending.find(p.recvNext)) {
        TransportEvent ev;
        ev.kind = TransportEvent::Message;
        ev.peer = p.id;
        ev.reliable = true;
        ev.data = std::move(it->second);
        p.pending.erase(it);
        ++p.recvNext;
        push(std::move(ev));
    }
}

void NativeTransport::addPeer(PlayerId id, const std::vector<std::string>& caps) {
    Peer& p = peers_[id];
    p.id = id;
    p.caps = caps;
    p.native = hasCap(caps, "native") || hasCap(caps, "udp");
    if (directWanted()) direct_.tokenFor(id);
}

void NativeTransport::dropPeer(PlayerId id) {
    peers_.erase(id);
    direct_.remove(id);
    for (size_t i = 0; i < syncPeers_.size(); ++i)
        if (syncPeers_[i] == id) {
            syncPeers_.erase(syncPeers_.begin() + long(i));
            break;
        }
}

void NativeTransport::signalDirect(Peer& p) {
    if (!directWanted()) return;
    JsValue udp = JsValue::object();
    udp.set("tok", hex64(direct_.tokenFor(p.id)));
    JsValue cands = JsValue::array();
    for (const std::string& c : direct_.candidates()) cands.push(c);
    udp.set("cands", cands);
    JsValue data = JsValue::object();
    data.set("udp", udp);
    JsValue m = JsValue::object();
    m.set("t", "signal");
    m.set("to", (long long)p.id);
    m.set("data", data);
    sendControl(m);
    p.signalled = true;
}

// ---- sending

void NativeTransport::relayFrame(PlayerId to, bool reliable, const std::vector<uint8_t>& frame) {
    std::vector<uint8_t> out;
    out.reserve(frame.size() + 5);
    put32(out, to);
    out.push_back(reliable ? 1 : 0);
    out.insert(out.end(), frame.begin(), frame.end());
    // An unguaranteed frame is droppable: while the socket is backed up the newest one to the
    // same peer replaces the waiting one (latest wins).
    ws_.sendBinary(out.data(), out.size(), !reliable, to);
}

bool NativeTransport::sendTo(PlayerId id, bool reliable, const uint8_t* data, size_t size) {
    auto it = peers_.find(id);
    if (it == peers_.end()) return false;
    Peer& p = it->second;
    const double t = now();
    std::vector<uint8_t> frame;
    if (reliable) {
        const uint32_t seq = ++p.sendSeq;
        if (directWanted() && direct_.sendReliable(id, seq, data, size, t)) {
            ++p.sentDirect;
            ++sentDirectTotal;
            return true;
        }
        frame.reserve(size + 5);
        frame.push_back(kRel);
        put32(frame, seq);
    } else {
        if (directWanted() && direct_.sendUnreliable(id, data, size, t)) {
            ++p.sentDirect;
            ++sentDirectTotal;
            return true;
        }
        frame.reserve(size + 1);
        frame.push_back(kUnrel);
    }
    if (!ws_.isOpen()) return false;
    frame.insert(frame.end(), data, data + size);
    relayFrame(id, reliable, frame);
    ++p.sentRelay;
    ++sentRelayTotal;
    return true;
}

bool NativeTransport::send(PlayerId to, bool reliable, const uint8_t* data, size_t size) {
    if (!self_ || vanished_) return false;
    if (to != 0) return sendTo(to, reliable, data, size);
    bool ok = true;
    for (auto& kv : peers_) ok = sendTo(kv.first, reliable, data, size) && ok;
    return ok;
}

bool NativeTransport::poll(TransportEvent& ev) {
    if (vanished_) return false;
    if (!servicedThisPoll_) {
        service();
        servicedThisPoll_ = true;
    }
    if (events_.empty()) {
        servicedThisPoll_ = false;
        return false;
    }
    ev = std::move(events_.front());
    events_.pop_front();
    return true;
}

void NativeTransport::describe(const SessionDesc& d) {
    if (!self_) return;
    JsValue m = JsValue::object();
    m.set("t", "describe");
    m.set("desc", descValue(d));
    sendControl(m);
}

void NativeTransport::kick(PlayerId id) {
    if (!self_) return;
    // The "kick" message removes the player at once: everything sent to it before goes ahead of
    // it over the relay (copies of what the direct path already delivered are dropped there).
    direct_.flushToRelay(id);
    JsValue m = JsValue::object();
    m.set("t", "kick");
    m.set("id", (long long)id);
    sendControl(m);
}

void NativeTransport::leave() {
    if (!self_) return;
    // The server tells the others at once: what is still on its way over the direct paths goes
    // ahead over the relay first (a "host quits" must arrive before the "left").
    direct_.flushToRelay(0);
    JsValue m = JsValue::object();
    m.set("t", "leave");
    sendControl(m);
    resetSession();
    state_ = ws_.isOpen() ? State::Connected : State::Failed;
}

// ---- state

std::vector<PeerLink> NativeTransport::links() const {
    std::vector<PeerLink> out;
    for (const auto& kv : peers_) {
        PeerLink l;
        l.id = kv.first;
        l.native = kv.second.native;
        l.direct = direct_.up(kv.first);
        l.path = direct_.pathOf(kv.first);
        l.rtt = direct_.rtt(kv.first);
        l.sentDirect = kv.second.sentDirect;
        l.sentRelay = kv.second.sentRelay;
        l.recvDirect = kv.second.recvDirect;
        l.recvRelay = kv.second.recvRelay;
        out.push_back(l);
    }
    return out;
}

std::string NativeTransport::statusJson() const {
    JsValue o = JsValue::object();
    o.set("state", stateName());
    o.set("server", url_);
    o.set("error", error_);
    o.set("code", code_);
    o.set("self", (long long)self_);
    o.set("host", (long long)host_);
    o.set("proto", serverProto_);
    o.set("udpPort", int(direct_.localPort()));
    JsValue peers = JsValue::array();
    for (const PeerLink& l : links()) {
        JsValue p = JsValue::object();
        p.set("id", (long long)l.id);
        p.set("native", l.native);
        p.set("direct", l.direct);
        p.set("path", l.path);
        p.set("rtt", l.rtt);
        p.set("sentDirect", l.sentDirect);
        p.set("sentRelay", l.sentRelay);
        p.set("recvDirect", l.recvDirect);
        p.set("recvRelay", l.recvRelay);
        peers.push(p);
    }
    o.set("peers", peers);
    return o.dump();
}

// ---- tests

void NativeTransport::vanish() {
    ws_.abort();
    direct_.close();
    events_.clear();
    peers_.clear();
    self_ = host_ = 0;
    vanished_ = true;
    state_ = State::Failed;
    error_ = "vanished";
}

void NativeTransport::syncBegin() {
    ++syncN_;
    if (syncN_ == 0) ++syncN_;
    syncServer_ = false;
    syncPeers_.clear();
    if (vanished_) return;
    if (ws_.isOpen()) {
        JsValue m = JsValue::object();
        m.set("t", "ping");
        m.set("n", (long long)syncN_);
        sendControl(m);
        syncServer_ = true;
    }
    if (directWanted()) syncPeers_ = direct_.ping(syncN_, now());
}

bool NativeTransport::syncDone() const { return !syncServer_ && syncPeers_.empty(); }

void NativeTransport::sockets(std::vector<sock::Wait>& out) const {
    if (vanished_) return;
    if (ws_.fd() != sock::kNoFd) {
        sock::Wait w;
        w.fd = ws_.fd();
        w.events = sock::kRead | (ws_.wantsWrite() ? sock::kWrite : 0);
        out.push_back(w);
    }
    if (direct_.fd() != sock::kNoFd) {
        sock::Wait w;
        w.fd = direct_.fd();
        w.events = sock::kRead;
        out.push_back(w);
    }
}

bool NativeTransport::openedQueued() const {
    for (const TransportEvent& e : events_)
        if (e.kind == TransportEvent::Opened || e.kind == TransportEvent::Closed) return true;
    return false;
}

// ---- LAN games

bool LanSearch::start(uint16_t port, std::string* error) {
    stop();
    port_ = port;
    found_.clear();
    fd_ = sock::udpOpen(sock::Addr::any(0), true, error);
    if (fd_ == sock::kNoFd) return false;
    lastAsk_ = -1;
    return true;
}

void LanSearch::stop() {
    if (fd_ != sock::kNoFd) sock::closeFd(fd_);
    fd_ = sock::kNoFd;
}

void LanSearch::ask() {
    const uint8_t q[8] = {'C', 'C', 'I', 'L', 1, 0, 0, 0};
    std::vector<sock::Addr> to = sock::broadcastIPv4(port_);
    to.push_back(sock::Addr::ipv4(0x7F000001u, port_));  // a LAN server on this machine
    for (const sock::Addr& a : to) sock::sendTo(fd_, q, sizeof q, a);
}

void LanSearch::service(double now, double every) {
    if (fd_ == sock::kNoFd) return;
    if (lastAsk_ < 0 || now - lastAsk_ >= every) {
        ask();
        lastAsk_ = now;
    }
    uint8_t buf[2048];
    for (int i = 0; i < 64; ++i) {
        sock::Addr from;
        const long n = sock::recvFrom(fd_, buf, sizeof buf, from);
        if (n < 0) break;
        if (n < 6 || std::memcmp(buf, "CCIL", 4) != 0 || buf[4] != 2) continue;
        JsValue o;
        if (!JsValue::parse(std::string(reinterpret_cast<const char*>(buf + 5), size_t(n - 5)), o) || !o.isObject()) continue;
        LanServer s;
        s.name = o["name"].str();
        s.port = uint16_t(o["port"].integer());
        s.proto = int(o["proto"].integer());
        s.more = o["more"].boolean();
        s.address = from.host();
        s.url = "ws://" + s.address + ":" + std::to_string(s.port) + "/net";
        for (const JsValue& x : o["sessions"].items()) {
            SessionInfo i;
            i.code = x["code"].str();
            i.name = x["name"].str();
            i.city = x["city"].str();
            i.version = x["version"].str();
            i.players = int(x["players"].integer());
            i.max = int(x["max"].integer(8));
            i.mode = int(x["mode"].integer());
            i.sealed = x["sealed"].boolean();
            i.password = x["password"].boolean();
            s.sessions.push_back(i);
        }
        // One entry per server: an answer through 127.0.0.1 and one through a LAN address are the
        // same server when the name and port agree (the LAN address is kept).
        bool merged = false;
        for (LanServer& f : found_) {
            if (f.port == s.port && f.name == s.name) {
                const bool keepOld = !sock::Addr::parseHost(f.address, 0, from) || !from.isLoopback();
                if (!keepOld || f.address == s.address) f = s;
                else f.sessions = s.sessions;
                merged = true;
                break;
            }
        }
        if (!merged) found_.push_back(s);
    }
}

}  // namespace net
}  // namespace cc
