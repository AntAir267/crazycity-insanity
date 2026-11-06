// The front end's connection to the network layer (our own; spec/game/multiplayer.md gives only
// the game's records and events): the session server's host / join / list, leaving, the
// session's code and invite address, and the transport a session runs on (net/transport.h).
//
// Implementations: the browser's (platform/web_net_link.cpp over web/net.js: WebSocket session
// server, WebRTC data channels) and the native one (the native-net builder's). Without one the
// Multiplayer screens say that network games are not available in this build.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "net/transport.h"

namespace cc {
namespace net {

class NetLink {
public:
    virtual ~NetLink() = default;
    // The session server players use unless they type another.
    virtual std::string defaultServer() const = 0;
    // Asynchronous: the transport reports Opened (or Closed with a reason); errors reaching the
    // server come back through takeError().
    virtual void host(const std::string& server, const SessionDesc& desc) = 0;
    virtual void join(const std::string& server, const std::string& code, const std::string& password,
                      const std::string& version) = 0;
    virtual void list(const std::string& server) = 0;  // the answer: takeSessions()
    virtual void leave() = 0;
    virtual std::string code() const = 0;              // the open session's four-letter code
    // An address that opens the game and joins `code` (the page's address with ?join=CODE).
    virtual std::string inviteUrl(const std::string& code, const std::string& server) const = 0;
    virtual bool takeSessions(std::vector<SessionInfo>& out) = 0;
    virtual bool takeError(std::string& line) = 0;

    // ---- natively (platform/native_net_link.*); the browser has none of these.
    // Every frame: the socket work while no session pumps (the game list, the LAN search).
    virtual void service() {}
    // "Host on this computer": the engine's own session server on its own thread, reachable from
    // this computer only or, in LAN mode, from the local network. Hosting then goes through it.
    virtual bool canHostHere() const { return false; }
    virtual bool hostHere() const { return false; }  // the remembered choice
    virtual void setHostHere(bool on) { (void)on; }
    virtual bool lan() const { return false; }       // the remembered LAN switch
    virtual void setLan(bool on) { (void)on; }
    // Starts (or restarts in the other mode) the server; false with a reason when the port is taken.
    virtual bool startLocalServer(std::string& error) {
        error = "not in this build";
        return false;
    }
    virtual void stopLocalServer() {}
    virtual bool localServerRunning() const { return false; }
    virtual std::string localServerUrl() const { return std::string(); }  // this computer's address of it
    virtual std::vector<std::string> localServerUrls() const { return {}; }  // the addresses friends use
    // How this machine reaches a player ("direct, 12 ms", "relayed", "browser, relayed"); "" unknown.
    virtual std::string linkTo(double playerId) const {
        (void)playerId;
        return std::string();
    }
    // The connection to the session server when it is not simply working ("connecting", "failed: …").
    virtual std::string statusLine() const { return std::string(); }
    // A typed server is remembered for the next time.
    virtual void rememberServer(const std::string& typed) { (void)typed; }
};

}  // namespace net
}  // namespace cc
