#include "platform/direct_link.h"

#include <algorithm>
#include <cstring>
#include <random>

namespace cc {
namespace net {

namespace {
enum Kind : uint8_t {
    Probe = 0x21,     // [u32 nonce]
    ProbeAck = 0x22,  // [u32 nonce]
    Rel = 0x23,       // [u32 seq] payload
    Unrel = 0x24,     // payload
    Ack = 0x25,       // [u32 everything up to][u32 this one]
    PingK = 0x26,     // [u32 n][f64 sent at]
    PongK = 0x27,     // the ping's body
};
constexpr size_t kHeader = 13;
// The session server's address answer: request "CCIW" 1 + padding (28 bytes, never smaller than the
// answer); answer "CCIW" 2, family 4 or 6, port (big-endian), the address.
const uint8_t kEcho[4] = {'C', 'C', 'I', 'W'};
constexpr size_t kEchoRequest = 28;

void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i)));
}
void put64(std::vector<uint8_t>& b, uint64_t v) {
    for (int i = 0; i < 8; ++i) b.push_back(uint8_t(v >> (8 * i)));
}
uint32_t get32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
uint64_t get64(const uint8_t* p) { return uint64_t(get32(p)) | (uint64_t(get32(p + 4)) << 32); }
void putF64(std::vector<uint8_t>& b, double d) {
    uint64_t v;
    std::memcpy(&v, &d, 8);
    put64(b, v);
}
double getF64(const uint8_t* p) {
    const uint64_t v = get64(p);
    double d;
    std::memcpy(&d, &v, 8);
    return d;
}
uint64_t randomToken() {
    std::random_device rd;
    uint64_t t = 0;
    while (t == 0) t = (uint64_t(rd()) << 32) ^ uint64_t(rd());
    return t;
}
}  // namespace

DirectLinks::DirectLinks() = default;
DirectLinks::~DirectLinks() { close(); }

bool DirectLinks::open(PlayerId self, const LinkConfig& cfg, Hooks hooks, std::string* error) {
    close();
    self_ = self;
    cfg_ = cfg;
    hooks_ = std::move(hooks);
    rng_ = cfg.faults.seed ? cfg.faults.seed : 1;
    fd_ = sock::udpOpen(sock::Addr::any(cfg.port), false, error);
    if (fd_ == sock::kNoFd) return false;
    sock::Addr a;
    sock::localAddr(fd_, a);
    port_ = a.port();
    candidatesChanged_ = false;
    return true;
}

void DirectLinks::close() {
    if (fd_ != sock::kNoFd) sock::closeFd(fd_);
    fd_ = sock::kNoFd;
    peers_.clear();
    held_.clear();
    reflexive_ = sock::Addr();
    server_ = sock::Addr();
    askedAt_ = -1;
    asks_ = 0;
}

float DirectLinks::rand01() {
    rng_ = rng_ * 214013u + 2531011u;
    return float((rng_ >> 16) & 0x7FFF) / 32768.0f;
}

std::vector<std::string> DirectLinks::candidates() const {
    std::vector<std::string> out;
    if (fd_ == sock::kNoFd) return out;
    std::vector<sock::Addr> mine;
    if (cfg_.lan)
        for (sock::Addr a : sock::localIPv4(false)) {
            a.setPort(port_);
            mine.push_back(a);
        }
    if (cfg_.loopback) mine.push_back(sock::Addr::ipv4(0x7F000001u, port_));
    if (reflexive_.valid()) {
        bool dup = false;
        for (const sock::Addr& a : mine) dup = dup || a == reflexive_;
        if (!dup) mine.insert(mine.begin(), reflexive_);
    }
    for (const sock::Addr& a : mine) out.push_back(a.str());
    if (out.size() > 8) out.resize(8);
    return out;
}

void DirectLinks::askReflexive(const sock::Addr& server, double now) {
    if (fd_ == sock::kNoFd || !server.valid() || !server.v4()) return;
    server_ = server;
    asks_ = 0;
    askedAt_ = -1;
    reflexive_ = sock::Addr();
    std::vector<uint8_t> b(kEcho, kEcho + 4);
    b.push_back(1);
    b.resize(kEchoRequest, 0);
    sock::sendTo(fd_, b.data(), b.size(), server_);
    askedAt_ = now;
    asks_ = 1;
}

bool DirectLinks::takeCandidatesChanged() {
    const bool c = candidatesChanged_;
    candidatesChanged_ = false;
    return c;
}

DirectLinks::Peer* DirectLinks::find(PlayerId id) {
    auto it = peers_.find(id);
    return it == peers_.end() ? nullptr : &it->second;
}
const DirectLinks::Peer* DirectLinks::find(PlayerId id) const {
    auto it = peers_.find(id);
    return it == peers_.end() ? nullptr : &it->second;
}

uint64_t DirectLinks::tokenFor(PlayerId peer) {
    Peer& p = peers_[peer];
    p.id = peer;
    if (!p.localToken) p.localToken = randomToken();
    return p.localToken;
}

void DirectLinks::remote(PlayerId peer, uint64_t token, const std::vector<std::string>& candidates, double now) {
    tokenFor(peer);
    Peer& p = peers_[peer];
    const bool changed = !p.haveRemote || p.remoteToken != token;
    p.remoteToken = token;
    p.haveRemote = true;
    for (const std::string& c : candidates) {
        sock::Addr a;
        if (!sock::Addr::parse(c, a) || !a.v4()) continue;
        bool dup = false;
        for (const sock::Addr& b : p.cands) dup = dup || a == b;
        if (!dup && p.cands.size() < 12) p.cands.push_back(a);
    }
    for (const sock::Addr& h : p.heardFrom) {
        bool dup = false;
        for (const sock::Addr& b : p.cands) dup = dup || h == b;
        if (!dup) p.cands.push_back(h);
    }
    p.heardFrom.clear();
    if (changed && p.isUp) {
        // A new token (the peer started over): the path has to be found again.
        p.isUp = false;
        if (hooks_.path) hooks_.path(peer, false);
    }
    if (!p.isUp) {
        p.gaveUp = false;
        p.probeStart = now;
        probe(p, now);
    }
}

void DirectLinks::remove(PlayerId peer) { peers_.erase(peer); }

void DirectLinks::removeAll() { peers_.clear(); }

bool DirectLinks::up(PlayerId peer) const {
    const Peer* p = find(peer);
    return p && p->isUp;
}

std::string DirectLinks::pathOf(PlayerId peer) const {
    const Peer* p = find(peer);
    return p && p->isUp ? p->path.str() : std::string();
}

double DirectLinks::rtt(PlayerId peer) const {
    const Peer* p = find(peer);
    return p ? p->srtt : -1;
}

void DirectLinks::header(std::vector<uint8_t>& b, uint8_t kind, const Peer& p) const {
    b.push_back(kind);
    put32(b, self_);
    put64(b, p.remoteToken);
}

void DirectLinks::out(const sock::Addr& to, std::vector<uint8_t> bytes, bool reliable, bool unreliable, double now) {
    const LinkFaults& f = cfg_.faults;
    if (f.blackhole) {
        ++stats.dropped;
        return;
    }
    if ((reliable && f.relLoss > 0 && rand01() < f.relLoss) || (unreliable && f.unrelLoss > 0 && rand01() < f.unrelLoss)) {
        ++stats.dropped;
        return;
    }
    if (unreliable && f.jitter > 0) {
        Held h;
        h.at = now + double(rand01()) * f.jitter;
        h.order = heldOrder_++;
        h.to = to;
        h.bytes = std::move(bytes);
        held_.push_back(std::move(h));
        ++stats.held;
        return;
    }
    sock::sendTo(fd_, bytes.data(), bytes.size(), to);
}

void DirectLinks::probe(Peer& p, double now) {
    p.lastProbe = now;
    if (!p.haveRemote || cfg_.faults.blackhole) return;
    for (const sock::Addr& a : p.cands) {
        std::vector<uint8_t> b;
        header(b, Probe, p);
        put32(b, uint32_t(now * 1000.0));
        sock::sendTo(fd_, b.data(), b.size(), a);
    }
}

void DirectLinks::setUp(Peer& p, const sock::Addr& at, double now) {
    p.path = at;
    p.lastHeard = now;
    p.lastSent = now;
    if (p.isUp) return;
    p.isUp = true;
    p.gaveUp = false;
    if (hooks_.path) hooks_.path(p.id, true);
}

void DirectLinks::lose(Peer& p, double now) {
    p.isUp = false;
    flushToRelay(p.id);
    if (hooks_.path) hooks_.path(p.id, false);
    p.gaveUp = false;
    p.probeStart = now;
    p.lastProbe = -1;
}

double DirectLinks::rto(const Peer& p, int tries) const {
    double base = p.srtt < 0 ? 0.2 : 2.0 * p.srtt + 0.05;
    base = std::max(0.1, std::min(1.0, base));
    return base * double(1 << std::min(tries, 3));
}

bool DirectLinks::sendReliable(PlayerId peer, uint32_t seq, const uint8_t* data, size_t size, double now) {
    Peer* p = find(peer);
    if (!p || !p->isUp || size > kMaxPayload || fd_ == sock::kNoFd) return false;
    std::vector<uint8_t> b;
    header(b, Rel, *p);
    put32(b, seq);
    b.insert(b.end(), data, data + size);
    Unacked& u = p->unacked[seq];
    u.data.assign(data, data + size);
    u.first = u.last = now;
    u.tries = 0;
    p->lastSent = now;
    ++stats.sentRel;
    out(p->path, std::move(b), true, false, now);
    return true;
}

bool DirectLinks::sendUnreliable(PlayerId peer, const uint8_t* data, size_t size, double now) {
    Peer* p = find(peer);
    if (!p || !p->isUp || size > kMaxPayload || fd_ == sock::kNoFd) return false;
    std::vector<uint8_t> b;
    header(b, Unrel, *p);
    b.insert(b.end(), data, data + size);
    p->lastSent = now;
    ++stats.sentUnrel;
    out(p->path, std::move(b), false, true, now);
    return true;
}

void DirectLinks::flushToRelay(PlayerId peer) {
    for (auto& kv : peers_) {
        Peer& p = kv.second;
        if (peer != 0 && p.id != peer) continue;
        for (auto& u : p.unacked) {
            if (hooks_.toRelay) hooks_.toRelay(p.id, u.first, u.second.data);
            ++stats.handedToRelay;
        }
        p.unacked.clear();
    }
}

std::vector<PlayerId> DirectLinks::ping(uint32_t n, double now) {
    std::vector<PlayerId> out;
    for (auto& kv : peers_) {
        Peer& p = kv.second;
        if (!p.isUp) continue;
        std::vector<uint8_t> b;
        header(b, PingK, p);
        put32(b, n);
        putF64(b, now);
        sock::sendTo(fd_, b.data(), b.size(), p.path);
        p.lastSent = now;
        out.push_back(p.id);
    }
    return out;
}

void DirectLinks::receive(const uint8_t* d, size_t n, const sock::Addr& from, double now) {
    if (n >= 8 && std::memcmp(d, kEcho, 4) == 0) {
        // The server's answer: where our datagrams come from.
        if (d[4] != 2 || !server_.valid() || from.ipv4() != server_.ipv4()) return;
        sock::Addr a;
        if (d[5] == 4 && n >= 12) {
            const uint16_t port = uint16_t((d[6] << 8) | d[7]);
            const uint32_t ip = (uint32_t(d[8]) << 24) | (uint32_t(d[9]) << 16) | (uint32_t(d[10]) << 8) | d[11];
            a = sock::Addr::ipv4(ip, port);
        } else {
            return;
        }
        if (a != reflexive_) {
            const std::vector<std::string> before = candidates();
            reflexive_ = a;
            if (candidates() != before) candidatesChanged_ = true;
        }
        return;
    }
    if (n < kHeader) return;
    const uint8_t kind = d[0];
    const PlayerId fromId = get32(d + 1);
    const uint64_t token = get64(d + 5);
    Peer* p = find(fromId);
    if (!p || !p->localToken || token != p->localToken) return;
    const uint8_t* body = d + kHeader;
    const size_t bn = n - kHeader;
    p->lastHeard = now;
    auto reply = [&](uint8_t k, const uint8_t* b, size_t len) {
        if (!p->haveRemote || cfg_.faults.blackhole) return;
        std::vector<uint8_t> out;
        header(out, k, *p);
        out.insert(out.end(), b, b + len);
        sock::sendTo(fd_, out.data(), out.size(), from);
    };
    switch (kind) {
        case Probe:
            if (!p->haveRemote) {
                bool dup = false;
                for (const sock::Addr& h : p->heardFrom) dup = dup || h == from;
                if (!dup && p->heardFrom.size() < 4) p->heardFrom.push_back(from);
                return;
            }
            reply(ProbeAck, body, bn);
            if (!p->isUp) {
                // A peer-seen address of the other side: worth probing too.
                bool dup = false;
                for (const sock::Addr& c : p->cands) dup = dup || c == from;
                if (!dup && p->cands.size() < 12) p->cands.push_back(from);
            }
            return;
        case ProbeAck:
            if (p->haveRemote) setUp(*p, from, now);
            return;
        case Rel: {
            if (bn < 4 || !p->haveRemote) return;
            const uint32_t seq = get32(body);
            ++stats.recvRel;
            if (hooks_.reliable) hooks_.reliable(fromId, seq, body + 4, bn - 4);
            uint8_t ack[8];
            const uint32_t cum = hooks_.delivered ? hooks_.delivered(fromId) : 0;
            for (int i = 0; i < 4; ++i) ack[i] = uint8_t(cum >> (8 * i)), ack[4 + i] = uint8_t(seq >> (8 * i));
            reply(Ack, ack, 8);
            if (p->isUp && from != p->path) p->path = from;
            return;
        }
        case Unrel:
            ++stats.recvUnrel;
            if (hooks_.unreliable) hooks_.unreliable(fromId, body, bn);
            if (p->isUp && from != p->path) p->path = from;
            return;
        case Ack: {
            if (bn < 8) return;
            const uint32_t cum = get32(body), one = get32(body + 4);
            auto it = p->unacked.find(one);
            if (it != p->unacked.end()) {
                if (it->second.tries == 0) {
                    const double sample = now - it->second.last;
                    p->srtt = p->srtt < 0 ? sample : p->srtt * 0.875 + sample * 0.125;
                }
                p->unacked.erase(it);
            }
            while (!p->unacked.empty() && p->unacked.begin()->first <= cum) p->unacked.erase(p->unacked.begin());
            return;
        }
        case PingK:
            reply(PongK, body, bn);
            return;
        case PongK: {
            if (bn < 12) return;
            const uint32_t k = get32(body);
            const double sent = getF64(body + 4);
            const double sample = now - sent;
            if (sample >= 0 && sample < 30) p->srtt = p->srtt < 0 ? sample : p->srtt * 0.875 + sample * 0.125;
            if (p->haveRemote && !p->isUp) setUp(*p, from, now);
            if (hooks_.pong) hooks_.pong(fromId, k);
            return;
        }
        default:
            return;
    }
}

void DirectLinks::service(double now) {
    if (fd_ == sock::kNoFd) return;
    uint8_t buf[2048];
    for (int i = 0; i < 4096; ++i) {
        sock::Addr from;
        const long n = sock::recvFrom(fd_, buf, sizeof buf, from);
        if (n < 0) break;
        if (cfg_.faults.blackhole) continue;
        receive(buf, size_t(n), from, now);
    }
    // Delayed datagrams whose time has come, earliest first.
    if (!held_.empty()) {
        std::sort(held_.begin(), held_.end(), [](const Held& a, const Held& b) { return a.at < b.at || (a.at == b.at && a.order < b.order); });
        size_t k = 0;
        while (k < held_.size() && held_[k].at <= now + 1e-9) {
            sock::sendTo(fd_, held_[k].bytes.data(), held_[k].bytes.size(), held_[k].to);
            ++k;
        }
        held_.erase(held_.begin(), held_.begin() + long(k));
    }
    if (server_.valid() && !reflexive_.valid() && asks_ > 0 && asks_ < 5 && now - askedAt_ >= 0.5) {
        std::vector<uint8_t> b(kEcho, kEcho + 4);
        b.push_back(1);
        b.resize(kEchoRequest, 0);
        sock::sendTo(fd_, b.data(), b.size(), server_);
        askedAt_ = now;
        ++asks_;
    }
    for (auto& kv : peers_) {
        Peer& p = kv.second;
        if (!p.haveRemote) continue;
        if (!p.isUp) {
            if (p.gaveUp) continue;
            if (p.probeStart < 0) p.probeStart = now;
            if (now - p.probeStart > cfg_.setupTimeout) {
                p.gaveUp = true;
                continue;
            }
            if (p.lastProbe < 0 || now - p.lastProbe >= cfg_.probeEvery) probe(p, now);
            continue;
        }
        if (now - p.lastHeard >= cfg_.deadAfter) {
            lose(p, now);
            continue;
        }
        for (auto it = p.unacked.begin(); it != p.unacked.end();) {
            Unacked& u = it->second;
            if (now - u.first >= cfg_.relayAfter) {
                if (hooks_.toRelay) hooks_.toRelay(p.id, it->first, u.data);
                ++stats.handedToRelay;
                it = p.unacked.erase(it);
                continue;
            }
            if (now - u.last >= rto(p, u.tries)) {
                std::vector<uint8_t> b;
                header(b, Rel, p);
                put32(b, it->first);
                b.insert(b.end(), u.data.begin(), u.data.end());
                ++u.tries;
                u.last = now;
                p.lastSent = now;
                ++stats.resent;
                out(p.path, std::move(b), true, false, now);
            }
            ++it;
        }
        if (now - p.lastSent >= cfg_.keepAlive) {
            std::vector<uint8_t> b;
            header(b, PingK, p);
            put32(b, 0);
            putF64(b, now);
            if (!cfg_.faults.blackhole) sock::sendTo(fd_, b.data(), b.size(), p.path);
            p.lastSent = now;
        }
    }
}

}  // namespace net
}  // namespace cc
