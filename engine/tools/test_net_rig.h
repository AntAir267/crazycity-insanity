// The network under the network game checks (tools/test_net.cpp): the loopback hub by default, or
// real local sockets (tools/test_net_sockets.cpp): the engine's own session server or Node's
// server/signal.js, native transports with or without the direct UDP path, faults on the
// datagrams. The rig calls afterFrame() after each machine's frame: a barrier that waits until
// what that machine sent has reached the others' sockets, so a run over sockets is as repeatable
// as one over the hub.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "net/transport.h"
#include "platform/direct_link.h"

namespace cc {
class Vfs;

struct RigNet {
    virtual ~RigNet() = default;
    // A new machine: the first hosts (with `d`), later ones join by the code (password, version).
    virtual net::Transport* connect(const net::SessionDesc& d) = 0;
    // A machine that vanishes without a word.
    virtual void vanish(net::PlayerId id) = 0;
    // The server's view: is the session sealed?
    virtual bool sealed() = 0;
    virtual void setTime(double t) = 0;
    virtual void afterFrame(net::Transport* t) = 0;
    virtual void beforeStep() = 0;
    // Counters for the log (direct and relayed messages, faults).
    virtual std::string summary() const = 0;
    // Barriers that ran into their time-out (should be none).
    virtual int stalls() const = 0;
};

struct SocketNetOptions {
    enum Server { Engine, Node };
    Server server = Engine;
    bool direct = true;          // native players look for the direct UDP path
    net::LinkFaults faults;      // on every machine's datagrams
    std::string nodeScript = "server/signal.js";
    std::string logPath;         // the Node server's output
};

std::unique_ptr<RigNet> makeSocketNet(const SocketNetOptions& o, std::string* error = nullptr);

// The socket-level checks: JSON, the WebSocket codec and handshake values, the two servers
// answering a script of messages alike, the guaranteed channel over a lossy direct path and its
// fallback to the relay, the latest-wins queue, wss:// through TLS. `buildDir` takes the test
// certificate and the Node server's logs.
void testNetSocketUnits(const std::function<void(bool, const std::string&)>& expect, const std::string& buildDir);

// Is Node there (for the Node server checks)?
bool haveNode();

}  // namespace cc
