// The engine's own session server: server/signal.js ported into the engine, so a native player can
// host without Node. One implementation for the headless `crazycity --server` (serverMain) and the
// in-game "host on this computer" (HostServer on its own thread). It speaks the same protocol as
// signal.js (proto 2): one WebSocket endpoint /net for hosting with a four-letter code, joining,
// the game list, signalling and the binary relay; /net/info; optionally a folder of static files
// (the page and the game data, with byte ranges). On the same port number over UDP it answers a
// native client's "where do my datagrams come from" (for the direct path) and, in LAN mode, LAN
// game searches from machines on the local network.
//
// By default it listens on 127.0.0.1 only; LAN mode (opt-in) listens on every interface.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "net/transport.h"

namespace cc {
namespace net {

struct HostServerConfig {
    uint16_t port = 8821;        // 0: any free port (port() tells which)
    bool lan = false;            // listen on every interface (other machines can join) instead of 127.0.0.1
    std::string bind;            // a specific address to listen on (overrides `lan`)
    std::string staticDir;       // serve this folder too (signal.js --static)
    bool udp = true;             // the UDP address answer (and the LAN search answer in LAN mode)
    std::string name;            // this server's name in LAN searches (default: the computer's name)
    bool quiet = false;          // no log lines
    std::function<void(const std::string&)> log;  // log lines (default: stdout, signal.js's format)
    uint32_t testIds = 0;        // tests: player ids from this number up and codes AAAA, AAAB, … (not random)
    int maxConnections = 512;
};

// What a session looks like from outside (for the host's own screens and the tests).
struct HostedSession {
    std::string code, name, password, city, version;
    int maxPlayers = 8, mode = 0;
    bool sealed = false;
    PlayerId host = 0;
    std::vector<PlayerId> order;
};

class HostServer {
public:
    HostServer();
    ~HostServer();
    HostServer(const HostServer&) = delete;
    HostServer& operator=(const HostServer&) = delete;

    // Binds the sockets. False (and `error`) when the port is taken or the address is wrong.
    bool start(const HostServerConfig& cfg, std::string* error = nullptr);
    // Waits up to `timeoutMs` for something to happen, then does all the work there is. Not with
    // startThread().
    void pump(int timeoutMs);
    // Runs the server on its own thread until stop().
    bool startThread();
    void stop();
    bool running() const;

    uint16_t port() const;
    // ws://…/net addresses other players can use (127.0.0.1 only, or each LAN address).
    std::vector<std::string> urls() const;
    std::vector<HostedSession> sessions() const;
    int connections() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// `crazycity --server` and `cctool netserver`: signal.js's options (--port, --host, --static,
// --quiet) plus --lan, --name, --no-udp; runs until interrupted. Returns the exit code.
int serverMain(const std::vector<std::string>& args);

}  // namespace net
}  // namespace cc
