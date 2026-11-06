// The direct path between two native players (our own protocol over UDP; browsers cannot use it,
// they stay on the server's relay). One UDP socket per machine serves every peer.
//
// Setting up: each side tells the other, through the session server's signalling, a random token
// and its candidate addresses (its LAN addresses, 127.0.0.1, and the address the server saw its
// UDP packets come from: no STUN service, the session server answers that question itself). Both
// sides then send probes to every candidate of the other; a probe carrying the right token is
// answered, and the first answer to arrive makes the path. Probes from both sides at once open the
// way through most home routers (hole punching). A path that hears nothing for a while is lost;
// its guaranteed messages go back over the relay and the probing starts again.
//
// Messages: guaranteed ones carry the transport's per-peer sequence number (the same one the
// relay path uses, so the receiver puts them in order and drops copies whichever way they came),
// are acknowledged and sent again until they are; after a while without an acknowledgement they
// are handed back to go over the relay. Unguaranteed ones (car states) are sent once.
//
// Every datagram: [u8 kind][u32 sender id][u64 receiver's token] + its body, little-endian.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "net/transport.h"
#include "platform/net_socket.h"

namespace cc {
namespace net {

// Faults put on the outgoing datagrams, for the tests (a fixed-seed generator, so a run repeats).
struct LinkFaults {
    float unrelLoss = 0;  // share of unguaranteed datagrams lost
    float relLoss = 0;    // share of guaranteed datagrams (first sends and resends) lost
    double jitter = 0;    // extra delay of each datagram, 0 … jitter seconds (reorders them)
    bool blackhole = false;  // nothing gets through (a path that dies)
    uint32_t seed = 1;
};

struct LinkConfig {
    uint16_t port = 0;           // 0: any free port
    bool lan = true;             // offer this machine's LAN addresses
    bool loopback = true;        // offer 127.0.0.1 (two programs on one machine)
    double probeEvery = 0.25;    // seconds between probes while setting up
    double setupTimeout = 10;    // seconds of probing before giving up (until new candidates)
    double deadAfter = 8;        // seconds without a datagram before the path counts as lost
    double relayAfter = 2;       // seconds a guaranteed message may wait for its acknowledgement
    double keepAlive = 1;        // seconds between keep-alives on a quiet path
    LinkFaults faults;
};

struct LinkStats {
    int sentRel = 0, sentUnrel = 0, resent = 0, recvRel = 0, recvUnrel = 0, handedToRelay = 0, dropped = 0, held = 0;
};

class DirectLinks {
public:
    // What the links hand back to the transport.
    struct Hooks {
        // A guaranteed message arrived (`seq` its transport sequence number).
        std::function<void(PlayerId from, uint32_t seq, const uint8_t* data, size_t size)> reliable;
        // The cumulative acknowledgement the receiver can give: every number up to it delivered.
        std::function<uint32_t(PlayerId from)> delivered;
        std::function<void(PlayerId from, const uint8_t* data, size_t size)> unreliable;
        // A guaranteed message that has to go over the relay after all.
        std::function<void(PlayerId to, uint32_t seq, const std::vector<uint8_t>& data)> toRelay;
        // The path to a peer came up or was lost.
        std::function<void(PlayerId peer, bool up)> path;
        // A test barrier's answer from a peer.
        std::function<void(PlayerId peer, uint32_t n)> pong;
    };

    DirectLinks();
    ~DirectLinks();
    bool open(PlayerId self, const LinkConfig& cfg, Hooks hooks, std::string* error = nullptr);
    void close();
    bool isOpen() const { return fd_ != sock::kNoFd; }
    sock::Fd fd() const { return fd_; }
    uint16_t localPort() const { return port_; }
    void setSelf(PlayerId self) { self_ = self; }
    LinkFaults& faults() { return cfg_.faults; }

    // Candidates of this machine ("ip:port"), the server-seen address included once known.
    std::vector<std::string> candidates() const;
    // Asks the session server's UDP port where this machine's datagrams come from.
    void askReflexive(const sock::Addr& server, double now);
    bool haveReflexive() const { return reflexive_.valid(); }
    // True once when the candidate list changed (to send it to the peers again).
    bool takeCandidatesChanged();

    // Peers: this side's token for one (made on first use), the other side's token and candidates.
    uint64_t tokenFor(PlayerId peer);
    void remote(PlayerId peer, uint64_t token, const std::vector<std::string>& candidates, double now);
    void remove(PlayerId peer);
    void removeAll();
    bool up(PlayerId peer) const;
    std::string pathOf(PlayerId peer) const;
    double rtt(PlayerId peer) const;
    // Biggest message that goes this way (bigger ones take the relay).
    static constexpr size_t kMaxPayload = 1200;

    // Sends over the path when it is up (false: take the relay).
    bool sendReliable(PlayerId peer, uint32_t seq, const uint8_t* data, size_t size, double now);
    bool sendUnreliable(PlayerId peer, const uint8_t* data, size_t size, double now);
    // Every unacknowledged guaranteed message to `peer` (0: all peers) handed to the relay now
    // (before a kick or leave, whose server message must come after them).
    void flushToRelay(PlayerId peer);
    // A test barrier to every peer with a path: their pongs come back through Hooks::pong.
    std::vector<PlayerId> ping(uint32_t n, double now);

    // Reads the socket and runs the timers (probes, resends, keep-alives, lost paths, delayed
    // datagrams).
    void service(double now);
    LinkStats stats;

private:
    struct Unacked {
        std::vector<uint8_t> data;
        double first = 0, last = 0;
        int tries = 0;
    };
    struct Peer {
        PlayerId id = 0;
        uint64_t localToken = 0, remoteToken = 0;
        bool haveRemote = false;
        std::vector<sock::Addr> cands;
        bool isUp = false;
        sock::Addr path;
        double lastHeard = -1, lastSent = -1;
        double probeStart = -1, lastProbe = -1;
        bool gaveUp = false;
        std::vector<sock::Addr> heardFrom;  // addresses that probed us before we knew the peer's token
        std::map<uint32_t, Unacked> unacked;
        double srtt = -1;
        uint32_t pingN = 0;
    };
    struct Held {
        double at = 0;
        uint64_t order = 0;
        sock::Addr to;
        std::vector<uint8_t> bytes;
    };

    Peer* find(PlayerId id);
    const Peer* find(PlayerId id) const;
    void header(std::vector<uint8_t>& b, uint8_t kind, const Peer& p) const;
    void out(const sock::Addr& to, std::vector<uint8_t> bytes, bool reliable, bool unreliable, double now);
    void probe(Peer& p, double now);
    void receive(const uint8_t* d, size_t n, const sock::Addr& from, double now);
    void setUp(Peer& p, const sock::Addr& at, double now);
    void lose(Peer& p, double now);
    double rto(const Peer& p, int tries) const;
    float rand01();

    sock::Fd fd_ = sock::kNoFd;
    uint16_t port_ = 0;
    PlayerId self_ = 0;
    LinkConfig cfg_;
    Hooks hooks_;
    std::map<PlayerId, Peer> peers_;
    sock::Addr reflexive_;
    sock::Addr server_;
    double askedAt_ = -1;
    int asks_ = 0;
    bool candidatesChanged_ = false;
    std::vector<Held> held_;
    uint64_t heldOrder_ = 0;
    uint32_t rng_ = 1;
};

}  // namespace net
}  // namespace cc
