#include "net/loopback.h"

#include <algorithm>

namespace cc {
namespace net {

bool LoopbackTransport::send(PlayerId to, bool reliable, const uint8_t* data, size_t size) {
    if (!hub_ || !open_) return false;
    TransportEvent ev;
    ev.kind = TransportEvent::Message;
    ev.peer = id_;
    ev.reliable = reliable;
    ev.data.assign(data, data + size);
    auto deliver = [&](PlayerId dst) {
        LoopbackTransport* t = hub_->find(dst);
        if (!t || !t->open_) return false;
        double delay = reliable ? hub_->latencyReliable_ : hub_->latencyUnreliable_;
        if (reliable) {
            ++hub_->sentReliable;
        } else {
            ++hub_->sentUnreliable;
            if (hub_->loss_ > 0.0f && hub_->random() < hub_->loss_) {
                ++hub_->dropped;
                return true;
            }
            if (hub_->jitter_ > 0.0) delay += double(hub_->random()) * hub_->jitter_;
        }
        hub_->post(dst, ev, delay);
        return true;
    };
    if (to != 0) return deliver(to);
    bool ok = true;
    for (PlayerId p : hub_->order_)
        if (p != id_) ok = deliver(p) && ok;
    return ok;
}

bool LoopbackTransport::poll(TransportEvent& ev) {
    if (!hub_) return false;
    // The earliest message whose time has come (ties in posting order). Guaranteed messages from
    // one sender keep their order because their latency is the same for all of them.
    size_t best = inbox_.size();
    for (size_t i = 0; i < inbox_.size(); ++i) {
        if (inbox_[i].at > hub_->now_ + 1e-9) continue;
        if (best == inbox_.size() || inbox_[i].at < inbox_[best].at ||
            (inbox_[i].at == inbox_[best].at && inbox_[i].order < inbox_[best].order))
            best = i;
    }
    if (best == inbox_.size()) return false;
    ev = std::move(inbox_[best].ev);
    inbox_.erase(inbox_.begin() + long(best));
    return true;
}

void LoopbackTransport::describe(const SessionDesc& d) {
    if (hub_ && hub_->host_ == id_) hub_->desc_ = d;
}

void LoopbackTransport::kick(PlayerId id) {
    if (hub_ && hub_->host_ == id_ && id != id_) hub_->removePlayer(id, false, "kicked");
}

void LoopbackTransport::leave() {
    if (hub_ && open_) hub_->removePlayer(id_, false, "left");
}

float LoopbackHub::random() {
    rng_ = rng_ * 214013u + 2531011u;
    return float((rng_ >> 16) & 0x7FFF) / 32768.0f;
}

LoopbackTransport* LoopbackHub::find(PlayerId id) {
    for (auto& e : ends_)
        if (e->id_ == id) return e.get();
    return nullptr;
}

void LoopbackHub::post(PlayerId to, TransportEvent ev, double delay) {
    LoopbackTransport* t = find(to);
    if (!t) return;
    LoopbackTransport::Queued q;
    q.at = now_ + delay;
    q.order = order_counter_++;
    q.ev = std::move(ev);
    t->inbox_.push_back(std::move(q));
}

LoopbackTransport* LoopbackHub::connect(const SessionDesc& d) {
    auto t = std::make_unique<LoopbackTransport>();
    t->hub_ = this;
    t->id_ = nextId_++;
    LoopbackTransport* raw = t.get();
    ends_.push_back(std::move(t));
    if (!haveSession_) {
        haveSession_ = true;
        desc_ = d;
        host_ = raw->id_;
    } else {
        std::string why;
        if (desc_.sealed) why = "sealed";
        else if (int(order_.size()) >= desc_.maxPlayers) why = "full";
        else if (!desc_.password.empty() && d.password != desc_.password) why = "password";
        else if (!d.version.empty() && !desc_.version.empty() && d.version != desc_.version) why = "version";
        if (!why.empty()) {
            TransportEvent ev;
            ev.kind = TransportEvent::Closed;
            ev.reason = "refused: " + why;
            post(raw->id_, ev, 0.0);
            return raw;
        }
    }
    raw->open_ = true;
    order_.push_back(raw->id_);
    TransportEvent op;
    op.kind = TransportEvent::Opened;
    op.self = raw->id_;
    op.host = host_;
    op.roster = order_;
    post(raw->id_, op, 0.0);
    for (PlayerId p : order_) {
        if (p == raw->id_) continue;
        TransportEvent j;
        j.kind = TransportEvent::PeerJoined;
        j.peer = raw->id_;
        post(p, j, latencyReliable_);
    }
    return raw;
}

void LoopbackHub::removePlayer(PlayerId id, bool vanished, const std::string& reason) {
    LoopbackTransport* t = find(id);
    if (!t || !t->open_) return;
    t->open_ = false;
    order_.erase(std::remove(order_.begin(), order_.end(), id), order_.end());
    if (!vanished) {
        TransportEvent c;
        c.kind = TransportEvent::Closed;
        c.reason = reason;
        post(id, c, 0.0);
    } else {
        t->inbox_.clear();
    }
    for (PlayerId p : order_) {
        TransportEvent l;
        l.kind = TransportEvent::PeerLeft;
        l.peer = id;
        post(p, l, latencyReliable_);
    }
    if (id == host_) {
        host_ = order_.empty() ? 0 : order_.front();
        if (host_ != 0)
            for (PlayerId p : order_) {
                TransportEvent h;
                h.kind = TransportEvent::HostChanged;
                h.host = host_;
                post(p, h, latencyReliable_);
            }
    }
}

void LoopbackHub::vanish(PlayerId id) { removePlayer(id, true, "lost"); }

}  // namespace net
}  // namespace cc
