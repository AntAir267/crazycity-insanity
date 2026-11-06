// An in-process session for the native tests: every endpoint is a Transport, the hub plays the
// session server (ids, session order, host migration, sealing, limits) and delivers messages after
// a set latency. Unguaranteed messages can be dropped and reordered with a fixed-seed generator,
// so a test run is repeatable.
#pragma once

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "net/transport.h"

namespace cc {
namespace net {

class LoopbackHub;

class LoopbackTransport : public Transport {
public:
    PlayerId self() const override { return id_; }
    bool send(PlayerId to, bool reliable, const uint8_t* data, size_t size) override;
    bool poll(TransportEvent& ev) override;
    void describe(const SessionDesc& d) override;
    void kick(PlayerId id) override;
    void leave() override;
    bool open() const { return open_; }

private:
    friend class LoopbackHub;
    struct Queued {
        double at = 0;
        uint64_t order = 0;
        TransportEvent ev;
    };
    LoopbackHub* hub_ = nullptr;
    PlayerId id_ = 0;
    bool open_ = false;
    std::vector<Queued> inbox_;
};

class LoopbackHub {
public:
    explicit LoopbackHub(uint32_t firstId = 1001) : nextId_(firstId) {}
    // A new machine: the first one creates the session (with `desc`) and is its host; later ones
    // join it (refused by the session description's rules: then the endpoint gets Closed).
    LoopbackTransport* connect(const SessionDesc& desc = SessionDesc());
    // A machine that vanishes without a word (its peers see PeerLeft; a vanished host is replaced
    // by the next player in session order).
    void vanish(PlayerId id);
    // Time (seconds) for the latency; messages are delivered once their time has come.
    void setTime(double t) { now_ = t; }
    double time() const { return now_; }
    void setLatency(double reliable, double unreliable) {
        latencyReliable_ = reliable;
        latencyUnreliable_ = unreliable;
    }
    // Unguaranteed messages: share lost (0 … 1) and extra random delay (seconds, reorders them).
    void setLoss(float share, double jitter) {
        loss_ = share;
        jitter_ = jitter;
    }
    PlayerId host() const { return host_; }
    const std::vector<PlayerId>& order() const { return order_; }
    const SessionDesc& desc() const { return desc_; }
    // Counters for the tests.
    int sentReliable = 0, sentUnreliable = 0, dropped = 0;

private:
    friend class LoopbackTransport;
    void post(PlayerId to, TransportEvent ev, double delay);
    void removePlayer(PlayerId id, bool vanished, const std::string& reason);
    LoopbackTransport* find(PlayerId id);
    float random();  // 0 … 1, fixed seed

    std::vector<std::unique_ptr<LoopbackTransport>> ends_;
    std::vector<PlayerId> order_;
    PlayerId host_ = 0;
    PlayerId nextId_;
    SessionDesc desc_;
    bool haveSession_ = false;
    double now_ = 0;
    double latencyReliable_ = 0, latencyUnreliable_ = 0;
    float loss_ = 0;
    double jitter_ = 0;
    uint32_t rng_ = 12345;
    uint64_t order_counter_ = 0;
};

}  // namespace net
}  // namespace cc
