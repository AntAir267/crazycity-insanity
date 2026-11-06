// The native network transport (net/transport.h), the counterpart of the browser's
// platform/web_transport.* + web/net.js: a WebSocket (ws:// or wss://) to the session server
// (server/signal.js or the engine's own platform/host_server.*) for hosting, joining by code, the
// game list, signalling and the binary relay, and between native players a direct UDP path
// (platform/direct_link.*) once one is found.
//
// Game messages use net.js's framing so browsers and native players read each other:
// guaranteed [1][u32 per-peer sequence number][message], unguaranteed [0][message]. Over the relay
// a frame goes as the server's binary message [u32 peer][u8 flags][frame] (flags bit 0:
// guaranteed; an unguaranteed one may be dropped, and while the socket is backed up a newer one
// to the same peer replaces it). Between native players with a direct path both kinds go over
// UDP; the receiver orders guaranteed messages by their number whichever path they took. A
// browser peer is always reached through the relay (the server tells each machine what the
// others can do: "webrtc" for a browser, "udp" for a native player).
//
// Everything runs on the caller's thread: poll() (Session::pump) and service() do the socket
// work without waiting. Before any session, service() keeps the connection and the game list
// going.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/js_value.h"
#include "net/transport.h"
#include "platform/direct_link.h"
#include "platform/websocket.h"

namespace cc {
namespace net {

// The session server protocol revision this side speaks (server/signal.js: 2 adds the
// capabilities, the welcome message, ping and the UDP address answer).
constexpr int kNetProtocol = 2;
// The game version joiners must agree on (the page sends the same).
constexpr const char* kGameVersion = "cci-1";
// The default session server port (server/signal.js and the engine's own server).
constexpr uint16_t kDefaultNetPort = 8821;

// A server address as a player types it ("192.168.1.5", "host:9000", "[::1]", "wss://x/net") as a
// WebSocket URL: ws:// unless given, the default port, the path /net when none is given.
std::string serverUrl(const std::string& typed);

struct NativeNetConfig {
    std::string server = "ws://127.0.0.1:8821/net";
    bool direct = true;         // look for a direct UDP path to other native players
    LinkConfig link;            // the direct path's settings (port, LAN and loopback candidates, timers, test faults)
    ws::ClientOptions ws;       // TLS checks, a CA file, the connect time-out
};

// (SessionInfo, one game of the server's list: net/transport.h.)

// How this machine reaches one peer.
struct PeerLink {
    PlayerId id = 0;
    bool native = false;     // the peer said "udp" (a native player); otherwise a browser
    bool direct = false;     // the direct UDP path is up
    std::string path;        // its address when direct
    double rtt = -1;         // seconds (direct path only)
    int sentDirect = 0, sentRelay = 0, recvDirect = 0, recvRelay = 0;
};

class NativeTransport : public Transport {
public:
    enum class State {
        Idle,        // nothing yet
        Connecting,  // to the server
        Connected,   // to the server, not in a session (the game list works)
        Waiting,     // host or join asked, no answer yet
        InSession,
        Failed,      // the connection failed or broke (error())
    };

    explicit NativeTransport(NativeNetConfig cfg = NativeNetConfig());
    ~NativeTransport() override;
    NativeTransport(const NativeTransport&) = delete;
    NativeTransport& operator=(const NativeTransport&) = delete;

    // ---- the server
    // Connects to `url` (empty: the configured server); host, join and the list connect first
    // when needed. False on a bad address.
    bool connect(const std::string& url = std::string());
    // Asks to host a session with `d` (Opened follows with the code, or Closed).
    void host(const SessionDesc& d);
    // Asks to join by code (Opened follows, or Closed "refused: …").
    void join(const std::string& code, const std::string& password = std::string(), const std::string& version = kGameVersion);
    // Asks for the game list; takeSessionList() hands it over when it arrives.
    void requestList();
    bool takeSessionList(std::vector<SessionInfo>& out);
    // Leaves any session and closes the connection.
    void disconnect();
    // The socket work (poll() does it too).
    void service();
    // The clock for time-outs and the direct path's timers (default: a steady real clock).
    void setClock(std::function<double()> clock) { clock_ = std::move(clock); }
    double now() const;

    // ---- state for the front end
    State state() const { return state_; }
    const char* stateName() const;
    const std::string& error() const { return error_; }
    const std::string& serverUrl() const { return url_; }
    const std::string& code() const { return code_; }
    PlayerId host() const { return host_; }
    int serverProtocol() const { return serverProto_; }
    std::vector<PeerLink> links() const;
    // {"state", "server", "error", "code", "self", "host", "udpPort", "peers":[…]} for a status line.
    std::string statusJson() const;
    const LinkStats& directStats() const { return direct_.stats; }
    // Game messages sent over the direct path and over the relay, since the start.
    int sentDirectTotal = 0, sentRelayTotal = 0;
    DirectLinks& directLinks() { return direct_; }

    // ---- Transport
    PlayerId self() const override { return self_; }
    bool send(PlayerId to, bool reliable, const uint8_t* data, size_t size) override;
    bool poll(TransportEvent& ev) override;
    void describe(const SessionDesc& d) override;
    void kick(PlayerId id) override;
    void leave() override;

    // ---- for the tests
    // Drops every socket without a word: the server sees the connection lost; no more events.
    void vanish();
    // A barrier: a ping through the server and one over each direct path; done when every answer
    // is back (everything this machine sent before has then been handled by the server and read
    // by the direct peers).
    void syncBegin();
    bool syncDone() const;
    // The sockets to wait on.
    void sockets(std::vector<sock::Wait>& out) const;
    // The transport still has this peer (its "left" not seen yet).
    bool knowsPeer(PlayerId id) const { return peers_.count(id) != 0; }
    // Messages queued for the engine (not yet polled).
    size_t queuedEvents() const { return events_.size(); }
    bool openedQueued() const;

private:
    struct Peer {
        PlayerId id = 0;
        std::vector<std::string> caps;
        bool native = false;
        bool signalled = false;  // our direct-path details sent
        uint32_t sendSeq = 0;    // guaranteed messages sent to it
        uint32_t recvNext = 1;   // next guaranteed number expected
        std::map<uint32_t, std::vector<uint8_t>> pending;
        int sentDirect = 0, sentRelay = 0, recvDirect = 0, recvRelay = 0;
    };

    void ensureConnected();
    void sendControl(const JsValue& v);
    bool sendTo(PlayerId to, bool reliable, const uint8_t* data, size_t size);
    JsValue capsValue() const;
    void onText(const std::string& text);
    void onBinary(const uint8_t* d, size_t n);
    void onFrame(PlayerId from, const uint8_t* d, size_t n, bool direct);
    void deliverReliable(Peer& p, uint32_t seq, const uint8_t* d, size_t n);
    void addPeer(PlayerId id, const std::vector<std::string>& caps);
    void dropPeer(PlayerId id);
    void signalDirect(Peer& p);
    void relayFrame(PlayerId to, bool reliable, const std::vector<uint8_t>& frame);
    void push(TransportEvent ev) { events_.push_back(std::move(ev)); }
    void lostServer(const std::string& why);
    void resetSession();
    bool directWanted() const { return cfg_.direct && direct_.isOpen(); }

    NativeNetConfig cfg_;
    std::function<double()> clock_;
    ws::Client ws_;
    std::string url_;
    State state_ = State::Idle;
    std::string error_;
    bool wantConnected_ = false;
    int serverProto_ = 0;
    uint16_t serverUdp_ = 0;
    PlayerId self_ = 0, host_ = 0;
    std::string code_;
    std::map<PlayerId, Peer> peers_;
    std::deque<TransportEvent> events_;
    bool servicedThisPoll_ = false;
    std::vector<SessionInfo> list_;
    bool haveList_ = false;
    DirectLinks direct_;
    bool vanished_ = false;
    // The test barrier.
    uint32_t syncN_ = 0;
    bool syncServer_ = false;
    std::vector<PlayerId> syncPeers_;
};

// ---- LAN games

// A session server in LAN mode found on the local network, with its games.
struct LanServer {
    std::string name;     // the server's name (its computer's name by default)
    std::string address;  // its IP address
    uint16_t port = 0;
    std::string url;      // ws://address:port/net
    int proto = 0;
    std::vector<SessionInfo> sessions;
    bool more = false;    // more games than fit in the answer (ask the server for its list)
};

// Asks the local network for session servers in LAN mode (a UDP broadcast to their port, once a
// second while searching) and collects the answers.
class LanSearch {
public:
    LanSearch() = default;
    ~LanSearch() { stop(); }
    LanSearch(const LanSearch&) = delete;
    LanSearch& operator=(const LanSearch&) = delete;
    bool start(uint16_t port = kDefaultNetPort, std::string* error = nullptr);
    void stop();
    bool searching() const { return fd_ != sock::kNoFd; }
    // Reads answers; asks again every `every` seconds. `now`: seconds.
    void service(double now, double every = 1.0);
    const std::vector<LanServer>& results() const { return found_; }

private:
    void ask();
    sock::Fd fd_ = sock::kNoFd;
    uint16_t port_ = 0;
    double lastAsk_ = -1;
    std::vector<LanServer> found_;
};

}  // namespace net
}  // namespace cc
