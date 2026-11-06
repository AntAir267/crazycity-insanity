// The network layer behind the game (our own design; spec/game/multiplayer.md specifies only the
// records and events): a session of peers with unique non-zero player ids in a fixed session
// order, guaranteed (in order, exactly once) and unguaranteed messages between them, and the
// session notices DirectPlay gave MM2 (a player joined or left, a new host, this machine dropped).
//
// Implementations: the browser's WebRTC data channels with a WebSocket signalling and relay server
// (platform/web_transport.*, web/net.js, server/signal.js) and an in-process loopback hub for the
// native tests (net/loopback.*).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cc {
namespace net {

using PlayerId = uint32_t;

struct TransportEvent {
    enum Kind {
        Opened,      // this machine is in the session: `self`, `host`, `roster` (session order)
        PeerJoined,  // `peer` joined (appended to the session order)
        PeerLeft,    // `peer` left or was lost
        HostChanged, // the transport made `host` the host (the old host vanished, 1.3 step 6)
        Message,     // `peer` sent `data` (`reliable`: a guaranteed message)
        Closed,      // this machine is out of the session (`reason`: "kicked", "lost", "refused: …")
    };
    Kind kind = Message;
    PlayerId peer = 0;
    PlayerId self = 0;
    PlayerId host = 0;
    std::vector<PlayerId> roster;
    bool reliable = true;
    std::vector<uint8_t> data;
    std::string reason;
};

// What the host asks of the session server (DirectPlay's session description).
struct SessionDesc {
    std::string name;       // shown in the session list
    std::string password;   // optional
    int maxPlayers = 8;     // 1.1: at most 8
    bool sealed = false;    // 1.3 step 4: nobody can join
    std::string city;       // the host's city (RaceDir), for the join test
    int mode = 0;
    std::string version;    // the game version; joiners must agree
};

// One game of the session server's list (signal.js's publicDesc).
struct SessionInfo {
    std::string code, name, city, version;
    int players = 0, max = 8, mode = 0;
    bool sealed = false, password = false;
    std::string server;  // a LAN game's server (ws://…/net); empty: the server that was asked
};

class Transport {
public:
    virtual ~Transport() = default;
    // This machine's player id (0 until Opened).
    virtual PlayerId self() const = 0;
    // Sends to one peer (to = 0: every other peer). False when the peer is unreachable.
    virtual bool send(PlayerId to, bool reliable, const uint8_t* data, size_t size) = 0;
    bool send(PlayerId to, bool reliable, const std::vector<uint8_t>& b) { return send(to, reliable, b.data(), b.size()); }
    // The next event, in arrival order. False when none is waiting.
    virtual bool poll(TransportEvent& ev) = 0;
    // Host: the session description the server keeps (sealing, player limit, password).
    virtual void describe(const SessionDesc& d) = 0;
    // Host: remove a player from the session (1.3 step 8).
    virtual void kick(PlayerId id) = 0;
    // Leave the session.
    virtual void leave() = 0;
};

}  // namespace net
}  // namespace cc
