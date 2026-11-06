// WebSocket (RFC 6455) for the native network code: the frame codec shared by the client and the
// embedded server (platform/host_server.*), the opening handshake's key and accept values, and a
// non-blocking client for ws:// and wss:// (TLS through OpenSSL; CC_NET_NO_TLS builds without it).
//
// Outgoing messages queue in order. A message marked droppable with a key (the relayed car
// states, keyed by the peer) is "latest wins": while it waits in the queue (the socket is backed
// up) a newer droppable message with the same key replaces it in place.
#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "platform/net_socket.h"

namespace cc {
namespace ws {

// ---- helpers

void sha1(const void* data, size_t size, uint8_t out[20]);
std::string base64(const uint8_t* data, size_t size);
// Sec-WebSocket-Accept for a Sec-WebSocket-Key.
std::string acceptKey(const std::string& key);
// 16 random bytes in base64.
std::string newKey();

enum Op : uint8_t { Continuation = 0, Text = 1, Binary = 2, Close = 8, Ping = 9, Pong = 10 };

// A frame's bytes (FIN set; masked with a random key when `mask`).
void encodeFrame(std::vector<uint8_t>& out, Op op, const uint8_t* payload, size_t size, bool mask);

// Incoming bytes to whole messages (fragments joined; control frames handed over at once).
class Decoder {
public:
    struct Message {
        Op op = Binary;  // Text, Binary, Close, Ping or Pong
        std::vector<uint8_t> data;
    };
    explicit Decoder(size_t maxMessage = 16u << 20) : max_(maxMessage) {}
    // False on a protocol error (too big, a bad opcode): the connection should close.
    bool feed(const uint8_t* data, size_t size, std::vector<Message>& out);

private:
    std::vector<uint8_t> buf_;
    size_t at_ = 0;
    std::vector<uint8_t> parts_;
    Op partOp_ = Binary;
    bool inMessage_ = false;
    size_t max_;
};

// An outgoing byte queue with the latest-wins rule.
class OutQueue {
public:
    void push(std::vector<uint8_t> bytes, bool droppable = false, uint32_t key = 0);
    bool empty() const { return q_.empty(); }
    size_t bytes() const { return bytes_; }
    // Writes as much as the socket takes (through `write`, returning bytes written, 0 when it would
    // block, < 0 on an error). False on an error.
    template <typename W>
    bool flush(W&& write) {
        while (!q_.empty()) {
            Item& it = q_.front();
            it.tried = true;
            const long n = write(it.bytes.data() + it.sent, it.bytes.size() - it.sent);
            if (n < 0) return false;
            if (n == 0) return true;
            it.sent += size_t(n);
            bytes_ -= size_t(n);
            if (it.sent == it.bytes.size()) q_.pop_front();
        }
        return true;
    }
    void clear() {
        q_.clear();
        bytes_ = 0;
    }
    int replaced = 0;  // droppable messages replaced by newer ones

private:
    struct Item {
        std::vector<uint8_t> bytes;
        size_t sent = 0;
        bool tried = false;  // a write was attempted (TLS must retry the same bytes)
        bool droppable = false;
        uint32_t key = 0;
    };
    std::deque<Item> q_;
    size_t bytes_ = 0;
};

// ---- a parsed ws:// or wss:// address

struct Url {
    bool tls = false;
    std::string host;  // without brackets
    uint16_t port = 0;
    std::string path = "/";
    // The Host header value.
    std::string hostHeader() const;
    static bool parse(const std::string& text, Url& out, std::string* error = nullptr);
};

// ---- the client

struct ClientOptions {
    bool verifyTls = true;      // check the server's certificate and name (wss://)
    std::string caFile;         // a PEM bundle to trust instead of the system's
    double connectTimeout = 10; // seconds for the TCP, TLS and WebSocket handshakes
    std::string origin;         // an Origin header (none by default)
};

class Client {
public:
    enum State { Idle, Resolving, Connecting, TlsHandshake, Upgrading, Open, Closed };
    Client();
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    // Starts connecting (name lookup on a helper thread). False at once on a bad address.
    bool open(const std::string& url, const ClientOptions& opt = ClientOptions());
    // Does the work: connecting, handshakes, writing the queue, reading messages. `now`: seconds
    // (for the connect time-out).
    void service(double now);
    State state() const { return state_; }
    bool isOpen() const { return state_ == Open; }
    const std::string& error() const { return error_; }
    const Url& url() const { return url_; }

    void sendText(const std::string& s);
    void sendBinary(const uint8_t* data, size_t size, bool droppable = false, uint32_t key = 0);
    // The next whole text or binary message (pings answered, pongs ignored inside).
    bool next(Decoder::Message& m);
    // Bytes waiting to go out.
    size_t queued() const { return out_.bytes(); }
    int replaced() const { return out_.replaced; }
    // A clean close (close frame, then the socket) or, for the tests, a sudden one.
    void close();
    void abort();
    sock::Fd fd() const { return fd_; }
    // Wants to write (for waiting on the socket).
    bool wantsWrite() const;
    // The server's address once connected.
    const sock::Addr& peer() const { return peerAddr_; }

private:
    void fail(const std::string& why);
    void startConnect(double now);
    bool tlsStart();
    bool tlsStep();
    bool sendUpgrade();
    long rawWrite(const uint8_t* p, size_t n);
    long rawRead(uint8_t* p, size_t n);
    void readAll();

    State state_ = Idle;
    std::string error_;
    Url url_;
    ClientOptions opt_;
    struct Lookup;
    std::shared_ptr<Lookup> lookup_;
    std::vector<sock::Addr> addrs_;
    size_t addrAt_ = 0;
    sock::Fd fd_ = sock::kNoFd;
    sock::Addr peerAddr_;
    double startedAt_ = -1;
    std::string key_;
    std::string httpIn_;
    Decoder dec_;
    OutQueue out_;
    std::deque<Decoder::Message> in_;
    bool closing_ = false;
    struct Tls;
    std::unique_ptr<Tls> tls_;
};

}  // namespace ws
}  // namespace cc
