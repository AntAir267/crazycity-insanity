// The browser's network transport (net/transport.h): web/net.js keeps the WebSocket to the session
// server and the WebRTC data channels; it pushes the transport events in here (cc_net_push) and
// this side sends through it. Natively a stub that is never opened.
#pragma once

#include <deque>
#include <string>

#include "net/transport.h"

namespace cc {
namespace net {

class WebTransport : public Transport {
public:
    WebTransport();
    ~WebTransport() override;
    // The transport web/net.js pushes into (the newest one alive).
    static WebTransport* current();
    PlayerId self() const override { return self_; }
    bool send(PlayerId to, bool reliable, const uint8_t* data, size_t size) override;
    bool poll(TransportEvent& ev) override;
    void describe(const SessionDesc& d) override;
    void kick(PlayerId id) override;
    void leave() override;

    // From the page: kind 0 opened, 1 joined, 2 left, 3 host, 4 message, 5 closed; `json` holds
    // self, host, roster and reason.
    void push(int kind, PlayerId peer, bool reliable, const uint8_t* data, size_t size, const std::string& json);

private:
    PlayerId self_ = 0;
    std::deque<TransportEvent> queue_;
};

}  // namespace net
}  // namespace cc
