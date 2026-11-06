// The network checks over real local sockets (tools/test_net_rig.h): the rig's socket network
// (the engine's session server or Node's, native transports, the per-frame barrier) and the
// socket-level checks.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include "core/js_value.h"
#include "core/strutil.h"
#include "platform/direct_link.h"
#include "platform/host_server.h"
#include "platform/native_transport.h"
#include "platform/net_socket.h"
#include "platform/websocket.h"
#include "tools/test_net_rig.h"

namespace cc {

namespace {

double realNow() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// ---------------------------------------------------------------------------------------------
// A Node server/signal.js process

struct NodeServer {
    pid_t pid = -1;
    uint16_t port = 0;
    std::string log;

    bool start(const std::string& script, const std::string& logPath, const std::vector<std::string>& extra, std::string* err,
               const char* scheme = "http") {
        log = logPath;
        std::remove(logPath.c_str());
        pid = fork();
        if (pid < 0) {
            if (err) *err = "fork failed";
            return false;
        }
        if (pid == 0) {
            // Its own session without a terminal, input from /dev/null: Node must not stop on
            // terminal signals when the checks run in a background process group.
            setsid();
            const int in = open("/dev/null", O_RDONLY);
            if (in >= 0) {
                dup2(in, 0);
                close(in);
            }
            const int fd = open(logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0) {
                dup2(fd, 1);
                dup2(fd, 2);
                close(fd);
            }
            std::vector<std::string> a = {"node", script, "--port", "0", "--test-ids"};
            a.insert(a.end(), extra.begin(), extra.end());
            std::vector<char*> argv;
            for (std::string& s : a) argv.push_back(&s[0]);
            argv.push_back(nullptr);
            execvp("node", argv.data());
            _exit(127);
        }
        const std::string mark = std::string("network server on ") + scheme + "://127.0.0.1:";
        const double until = realNow() + 15.0;
        while (realNow() < until) {
            std::ifstream f(logPath);
            std::string line;
            while (std::getline(f, line)) {
                const size_t at = line.find(mark);
                if (at != std::string::npos) {
                    port = uint16_t(std::atoi(line.c_str() + at + mark.size()));
                    if (port) return true;
                }
            }
            int status = 0;
            if (waitpid(pid, &status, WNOHANG) == pid) {
                pid = -1;
                if (err) *err = "node exited (see " + logPath + ")";
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (err) *err = "node did not start (see " + logPath + ")";
        stop();
        return false;
    }
    void stop() {
        if (pid > 0) {
            kill(pid, SIGTERM);
            int status = 0;
            waitpid(pid, &status, 0);
        }
        pid = -1;
    }
    ~NodeServer() { stop(); }
};

// ---------------------------------------------------------------------------------------------
// The rig's socket network

class SocketNet : public RigNet {
public:
    explicit SocketNet(const SocketNetOptions& o) : o_(o) {}
    ~SocketNet() override {
        lister_.reset();
        ts_.clear();
        if (server_) server_->stop();
        node_.stop();
    }

    bool start(std::string* err) {
        if (o_.server == SocketNetOptions::Engine) {
            server_ = std::make_unique<net::HostServer>();
            net::HostServerConfig c;
            c.port = 0;
            c.testIds = 1001;
            c.quiet = true;
            if (!server_->start(c, err)) return false;
            url_ = "ws://127.0.0.1:" + std::to_string(server_->port()) + "/net";
            return true;
        }
        if (!node_.start(o_.nodeScript, o_.logPath.empty() ? std::string("build/net_node_server.log") : o_.logPath, {}, err)) return false;
        url_ = "ws://127.0.0.1:" + std::to_string(node_.port) + "/net";
        return true;
    }

    net::Transport* connect(const net::SessionDesc& d) override {
        net::NativeNetConfig cfg;
        cfg.server = url_;
        cfg.direct = o_.direct;
        cfg.link.lan = false;  // the loopback candidate only: the same candidates on every run
        cfg.link.loopback = true;
        cfg.link.faults = o_.faults;
        cfg.link.faults.seed = o_.faults.seed + uint32_t(ts_.size()) * 7919u;
        auto t = std::make_unique<net::NativeTransport>(cfg);
        t->setClock([this]() { return now_; });
        net::NativeTransport* raw = t.get();
        ts_.push_back(std::move(t));
        raw->connect(url_);
        if (code_.empty()) raw->host(d);
        else raw->join(code_, d.password, d.version);
        settle([&]() { return raw->openedQueued(); });
        if (code_.empty() && raw->state() == net::NativeTransport::State::InSession) code_ = raw->code();
        barrier(raw);
        return raw;
    }

    void vanish(net::PlayerId id) override {
        net::NativeTransport* v = nullptr;
        for (auto& t : ts_)
            if (t->self() == id) v = t.get();
        if (!v) return;
        v->vanish();
        settle([&]() {
            for (auto& t : ts_)
                if (t.get() != v && t->self() && t->knowsPeer(id)) return false;
            return true;
        });
        for (auto& t : ts_)
            if (t.get() != v && t->self()) barrier(t.get());
    }

    bool sealed() override {
        if (!lister_) {
            net::NativeNetConfig cfg;
            cfg.server = url_;
            cfg.direct = false;
            lister_ = std::make_unique<net::NativeTransport>(cfg);
            lister_->setClock([this]() { return now_; });
            lister_->connect(url_);
        }
        lister_->requestList();
        std::vector<net::SessionInfo> list;
        settle([&]() { return lister_->takeSessionList(list); });
        for (const net::SessionInfo& s : list)
            if (s.code == code_) return s.sealed;
        return false;
    }

    void setTime(double t) override { now_ = t; }

    void afterFrame(net::Transport* t) override { barrier(static_cast<net::NativeTransport*>(t)); }

    // What the scenario sent between frames (a chat line, a quit) reaches everyone before the
    // next frames, as the hub delivers it at once.
    void beforeStep() override {
        std::vector<net::NativeTransport*> live;
        for (auto& t : ts_)
            if (t->self()) {
                t->syncBegin();
                live.push_back(t.get());
            }
        settle([&]() {
            for (net::NativeTransport* t : live)
                if (!t->syncDone()) return false;
            return true;
        });
    }

    std::string summary() const override {
        long direct = 0, relay = 0;
        net::LinkStats s;
        for (const auto& t : ts_) {
            direct += t->sentDirectTotal;
            relay += t->sentRelayTotal;
            const net::LinkStats& x = t->directStats();
            s.sentRel += x.sentRel;
            s.sentUnrel += x.sentUnrel;
            s.resent += x.resent;
            s.recvRel += x.recvRel;
            s.recvUnrel += x.recvUnrel;
            s.handedToRelay += x.handedToRelay;
            s.dropped += x.dropped;
            s.held += x.held;
        }
        return strf("messages sent direct %ld, relayed %ld; datagrams: guaranteed %d (resent %d, %d handed to the relay), "
                    "unguaranteed %d, lost %d, delayed %d",
                    direct, relay, s.sentRel, s.resent, s.handedToRelay, s.sentUnrel, s.dropped, s.held);
    }
    int stalls() const override { return stalls_; }

    long directMessages() const {
        long n = 0;
        for (const auto& t : ts_) n += t->sentDirectTotal;
        return n;
    }

private:
    void serviceAll() {
        if (server_) server_->pump(0);
        for (auto& t : ts_) t->service();
        if (lister_) lister_->service();
    }
    void waitBriefly() {
        if (server_) {
            server_->pump(1);
            return;
        }
        std::vector<sock::Wait> w;
        for (auto& t : ts_) t->sockets(w);
        sock::wait(w, 2);
    }
    // Pumps everything until `done` (at most 10 s of real time).
    template <typename F>
    bool settle(F&& done) {
        const double until = realNow() + 10.0;
        for (;;) {
            serviceAll();
            if (done()) return true;
            if (realNow() > until) {
                ++stalls_;
                return false;
            }
            waitBriefly();
        }
    }
    void barrier(net::NativeTransport* t) {
        if (!t) return;
        t->syncBegin();
        settle([&]() { return t->syncDone(); });
    }

    SocketNetOptions o_;
    std::unique_ptr<net::HostServer> server_;
    NodeServer node_;
    std::string url_;
    std::vector<std::unique_ptr<net::NativeTransport>> ts_;
    std::unique_ptr<net::NativeTransport> lister_;
    std::string code_;
    double now_ = 0;
    int stalls_ = 0;
};

// ---------------------------------------------------------------------------------------------
// Unit checks

using Expect = std::function<void(bool, const std::string&)>;

std::string hexOf(const std::vector<uint8_t>& b) {
    std::string s;
    for (uint8_t c : b) s += strf("%02x", c);
    return s;
}

void jsonChecks(const Expect& expect) {
    JsValue v;
    const bool ok = JsValue::parse("{\"a\":[1,2.5,\"x\\u00e9\\ud83d\\ude00\\n\"],\"b\":true,\"c\":null,\"d\":{\"e\":-0,\"f\":1e3}} ", v);
    expect(ok && v.dump() == "{\"a\":[1,2.5,\"x\xc3\xa9\xf0\x9f\x98\x80\\n\"],\"b\":true,\"c\":null,\"d\":{\"e\":0,\"f\":1000}}",
           "JSON: parse and print back (escapes, a surrogate pair, numbers as JavaScript prints them)");
    JsValue bad;
    expect(!JsValue::parse("{\"a\":}", bad) && !JsValue::parse("[1,]", bad) && !JsValue::parse("\"x", bad),
           "JSON: bad text refused");
    JsValue n;
    JsValue::parse("{\"s\":\" 12 \",\"e\":\"\",\"x\":\"3a\",\"z\":0,\"t\":\"0x10\"}", n);
    expect(n["s"].jsNumber() == 12 && n["e"].jsNumber() == 0 && std::isnan(n["x"].jsNumber()) && !n["z"].truthy() && n["t"].jsNumber() == 16 &&
               JsValue(3.0).jsString() == "3" && JsValue(0.1).jsString() == "0.1" && JsValue(nullptr).jsString() == "null" &&
               n["missing"].isUndefined(),
           "JSON: JavaScript's Number(), String() and truthiness for the server's parity");
    expect(utf16Slice("abc\xc3\xa9\xf0\x9f\x98\x80z", 5) == "abc\xc3\xa9" && utf16Slice("abc\xc3\xa9\xf0\x9f\x98\x80z", 6) == "abc\xc3\xa9\xf0\x9f\x98\x80",
           "slice(0, n) by UTF-16 units (an emoji counts 2)");
}

void wsCodecChecks(const Expect& expect) {
    uint8_t h[20];
    ws::sha1("abc", 3, h);
    const std::vector<uint8_t> hv(h, h + 20);
    expect(hexOf(hv) == "a9993e364706816aba3e25717850c26c9cd0d89d", "SHA-1(\"abc\") (FIPS 180 example)");
    expect(ws::acceptKey("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "Sec-WebSocket-Accept of RFC 6455's example key");
    // Frames of 5, 300 and 70000 bytes, masked, a fragmented text with a ping in between; fed one
    // byte at a time.
    std::vector<uint8_t> stream;
    std::vector<uint8_t> p5(5, 7), p300(300), p70k(70000);
    for (size_t i = 0; i < p300.size(); ++i) p300[i] = uint8_t(i);
    for (size_t i = 0; i < p70k.size(); ++i) p70k[i] = uint8_t(i * 31);
    ws::encodeFrame(stream, ws::Binary, p5.data(), p5.size(), true);
    ws::encodeFrame(stream, ws::Binary, p300.data(), p300.size(), false);
    ws::encodeFrame(stream, ws::Binary, p70k.data(), p70k.size(), true);
    // "hel" + ping + "lo" as two fragments.
    const uint8_t frag1[] = {0x01, 0x03, 'h', 'e', 'l'};
    const uint8_t ping[] = {0x89, 0x01, 'P'};
    const uint8_t frag2[] = {0x80, 0x02, 'l', 'o'};
    stream.insert(stream.end(), frag1, frag1 + sizeof frag1);
    stream.insert(stream.end(), ping, ping + sizeof ping);
    stream.insert(stream.end(), frag2, frag2 + sizeof frag2);
    ws::Decoder dec;
    std::vector<ws::Decoder::Message> got;
    bool fine = true;
    for (uint8_t b : stream) fine = dec.feed(&b, 1, got) && fine;
    expect(fine && got.size() == 5 && got[0].data == p5 && got[1].data == p300 && got[2].data == p70k && got[3].op == ws::Ping &&
               got[4].op == ws::Text && std::string(got[4].data.begin(), got[4].data.end()) == "hello",
           "WebSocket frames: 7-, 16- and 64-bit lengths, masked and not, fragments around a ping, one byte at a time");
    ws::Decoder small(1000);
    std::vector<uint8_t> big;
    ws::encodeFrame(big, ws::Binary, p70k.data(), 2000, false);
    std::vector<ws::Decoder::Message> none;
    expect(!small.feed(big.data(), big.size(), none), "a message above the limit is refused");
    // The latest-wins queue.
    ws::OutQueue q;
    q.push({1, 1}, true, 7);
    q.push({2}, false, 0);
    q.push({3, 3, 3}, true, 7);
    q.push({4}, true, 8);
    std::vector<uint8_t> written;
    q.flush([&](const uint8_t* p, size_t n) -> long {
        written.insert(written.end(), p, p + n);
        return long(n);
    });
    expect(q.replaced == 1 && written == std::vector<uint8_t>({3, 3, 3, 2, 4}),
           "latest wins: a waiting car state to peer 7 replaced in its place by the newer one; others kept in order");
    ws::OutQueue q2;
    q2.push({1}, true, 7);
    q2.flush([&](const uint8_t*, size_t) -> long { return 0; });  // the socket is full: tried once
    q2.push({9}, true, 7);
    expect(q2.replaced == 0, "a message already handed to the socket (TLS must repeat it) is not replaced");
    ws::Url u;
    expect(ws::Url::parse("wss://example.org/net", u) && u.tls && u.port == 443 && u.path == "/net" &&
               ws::Url::parse("ws://[::1]:8821/net?x", u) && u.host == "::1" && u.port == 8821 && u.hostHeader() == "[::1]:8821" &&
               !ws::Url::parse("http://x/net", u),
           "ws:// and wss:// addresses");
    expect(net::serverUrl("192.168.1.5") == "ws://192.168.1.5:8821/net" && net::serverUrl(" host:9000 ") == "ws://host:9000/net" &&
               net::serverUrl("::1") == "ws://[::1]:8821/net" && net::serverUrl("[::1]:7") == "ws://[::1]:7/net" &&
               net::serverUrl("wss://cci.example.org") == "wss://cci.example.org/net" &&
               net::serverUrl("https://cci.example.org/game/net") == "wss://cci.example.org/game/net" &&
               net::serverUrl("ws://10.0.0.2:8821/net") == "ws://10.0.0.2:8821/net",
           "a typed server address to a WebSocket URL (the default port 8821 and /net)");
}

// A raw client for the parity checks: everything it receives, in order, as text.
struct RawClient {
    ws::Client c;
    std::vector<std::string> got;
    uint32_t pingN = 0;
    void read() {
        ws::Decoder::Message m;
        while (c.next(m)) {
            if (m.op == ws::Text) got.push_back(std::string(m.data.begin(), m.data.end()));
            else got.push_back("bin " + hexOf(m.data));
        }
    }
};

struct ServerUnderTest {
    std::unique_ptr<net::HostServer> engine;
    NodeServer node;
    uint16_t port = 0;
    void pump() {
        if (engine) engine->pump(0);
    }
    void wait() {
        if (engine) engine->pump(1);
        else std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
};

// Pumps until every client's last ping is answered.
void settleClients(ServerUnderTest& s, std::vector<RawClient*> cs) {
    for (RawClient* c : cs) {
        if (c->c.state() == ws::Client::Closed) continue;
        ++c->pingN;
        c->c.sendText(strf("{\"t\":\"ping\",\"n\":%u}", c->pingN));
    }
    const double until = realNow() + 10;
    for (;;) {
        s.pump();
        bool all = true;
        for (RawClient* c : cs) {
            c->c.service(realNow());
            c->read();
            if (c->c.state() == ws::Client::Closed) continue;
            const std::string want = strf("{\"t\":\"pong\",\"n\":%u}", c->pingN);
            bool seen = false;
            for (const std::string& g : c->got) seen = seen || g == want;
            all = all && seen;
        }
        if (all || realNow() > until) return;
        s.wait();
    }
}

std::string httpGet(uint16_t port, const std::string& path, ServerUnderTest& s) {
    sock::Addr a = sock::Addr::ipv4(0x7F000001u, port);
    sock::Fd fd = sock::tcpConnect(a);
    if (fd == sock::kNoFd) return std::string();
    const std::string req = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    std::string resp;
    bool sent = false;
    const double until = realNow() + 5;
    while (realNow() < until) {
        s.pump();
        if (!sent && sock::connectResult(fd) == 1) {
            sock::send(fd, req.data(), req.size());
            sent = true;
        }
        char b[4096];
        const long n = sock::recv(fd, b, sizeof b);
        if (n > 0) resp.append(b, size_t(n));
        else if (n == sock::kClosed) break;
        s.wait();
    }
    sock::closeFd(fd);
    return resp;
}

std::string udpEcho(uint16_t port, ServerUnderTest& s, uint16_t* myPort) {
    sock::Fd fd = sock::udpOpen(sock::Addr::ipv4(0x7F000001u, 0));
    sock::Addr me;
    sock::localAddr(fd, me);
    *myPort = me.port();
    uint8_t req[28] = {'C', 'C', 'I', 'W', 1};
    sock::sendTo(fd, req, sizeof req, sock::Addr::ipv4(0x7F000001u, port));
    std::string out;
    const double until = realNow() + 5;
    while (realNow() < until && out.empty()) {
        s.pump();
        uint8_t b[64];
        sock::Addr from;
        const long n = sock::recvFrom(fd, b, sizeof b, from);
        if (n > 0) out = hexOf(std::vector<uint8_t>(b, b + n));
        else s.wait();
    }
    sock::closeFd(fd);
    return out;
}

// The same script of messages to a server; the transcript of what each client got.
std::string serverScript(ServerUnderTest& s) {
    const std::string url = "ws://127.0.0.1:" + std::to_string(s.port) + "/net";
    RawClient a, b, c;
    std::vector<RawClient*> all = {&a, &b, &c};
    for (RawClient* x : all) x->c.open(url);
    settleClients(s, all);
    // The speaker's ping first: then the server has handled its message and sent what follows
    // from it to the others before their pings.
    auto say = [&](RawClient& x, const std::string& text) {
        x.c.sendText(text);
        settleClients(s, {&x});
        settleClients(s, all);
    };
    say(a, "{\"t\":\"list\"}");
    say(a, "{\"t\":\"host\",\"desc\":{\"name\":\"Ann's game\",\"maxPlayers\":3,\"password\":\"pw\",\"version\":\"cci-1\",\"city\":\"london\","
           "\"mode\":2},\"caps\":[\"native\",\"udp\"],\"proto\":2}");
    say(b, "{\"t\":\"join\",\"code\":\"AAAA\",\"password\":\"wrong\",\"version\":\"cci-1\"}");
    say(b, "{\"t\":\"join\",\"code\":\"AAAA\",\"password\":\"pw\",\"version\":\"cci-0\"}");
    say(b, "{\"t\":\"join\",\"code\":\"ZZZZ\"}");
    say(b, "{\"t\":\"join\",\"code\":\"aaaa\",\"password\":\"pw\",\"version\":\"cci-1\"}");
    say(c, "{\"t\":\"list\"}");
    say(a, "{\"t\":\"describe\",\"desc\":{\"maxPlayers\":2,\"name\":\"\",\"mode\":\"1\"}}");
    say(c, "{\"t\":\"list\"}");
    say(c, "{\"t\":\"join\",\"code\":\"AAAA\",\"password\":\"pw\",\"caps\":[\"native\"]}");
    say(a, "{\"t\":\"describe\",\"desc\":{\"maxPlayers\":\"8\",\"sealed\":1}}");
    say(c, "{\"t\":\"join\",\"code\":\"AAAA\",\"password\":\"pw\"}");
    say(c, "{\"t\":\"list\"}");
    say(a, "{\"t\":\"describe\",\"desc\":{\"maxPlayers\":0,\"password\":\"\"}}");
    say(c, "{\"t\":\"join\",\"code\":\"AAAA\",\"caps\":[\"native\",\"udp\",\"\",7]}");
    say(b, "{\"t\":\"signal\",\"to\":1001,\"data\":{\"sdp\":{\"type\":\"offer\",\"sdp\":\"v=0\"}}}");
    say(a, "{\"t\":\"signal\",\"to\":\"1003\",\"data\":{\"udp\":{\"tok\":\"00ff\",\"cands\":[\"127.0.0.1:9\"]}}}");
    say(a, "{\"t\":\"signal\",\"to\":4242}");
    // Binary relay: guaranteed and not, to a player and to nobody.
    const uint8_t r1[] = {0xEA, 0x03, 0, 0, 1, 1, 9, 0, 0, 0, 0xAB};  // to 1002, guaranteed
    const uint8_t r2[] = {0xEB, 0x03, 0, 0, 0, 0, 0xCD};               // to 1003, not
    const uint8_t r3[] = {0x00, 0x10, 0, 0, 1, 0};                     // to nobody
    a.c.sendBinary(r1, sizeof r1);
    a.c.sendBinary(r2, sizeof r2, true, 1003);
    a.c.sendBinary(r3, sizeof r3);
    settleClients(s, {&a});
    settleClients(s, all);
    say(b, "{\"t\":\"kick\",\"id\":1003}");     // not the host
    say(a, "{\"t\":\"kick\",\"id\":1001}");     // itself
    say(a, "{\"t\":\"kick\",\"id\":\"1002\"}");
    say(b, "{\"t\":\"host\",\"desc\":{\"name\":\"B\"}}");
    say(c, "{\"t\":\"leave\"}");
    say(c, "{\"t\":\"list\"}");
    say(a, "{\"t\":\"leave\"}");
    say(a, "nonsense");
    say(a, "{\"t\":\"list\"}");
    b.c.abort();
    // The server notices the lost connection on its own time (Node: soon after).
    for (double until = realNow() + 0.3; realNow() < until;) s.wait();
    settleClients(s, {&a, &c});
    say(c, "{\"t\":\"list\"}");
    std::string out;
    for (size_t i = 0; i < all.size(); ++i) {
        out += strf("client %zu:\n", i);
        for (std::string g : all[i]->got) {
            // The welcome carries the server's UDP port: the same everywhere but the number.
            if (startsWith(g, "{\"t\":\"welcome\"")) {
                const size_t at = g.find("\"udp\":");
                if (at != std::string::npos) g = g.substr(0, at) + "\"udp\":P}";
            }
            out += "  " + g + "\n";
        }
    }
    const std::string info = httpGet(s.port, "/net/info", s);
    const size_t body = info.find("\r\n\r\n");
    std::string ib = body == std::string::npos ? info : info.substr(body + 4);
    if (toLower(info.substr(0, body)).find("transfer-encoding: chunked") != std::string::npos) {
        // Node's answer comes in chunks: join them.
        std::string joined;
        size_t at = 0;
        while (at < ib.size()) {
            const size_t eol = ib.find("\r\n", at);
            if (eol == std::string::npos) break;
            const size_t n = size_t(std::strtoul(ib.substr(at, eol - at).c_str(), nullptr, 16));
            if (n == 0) break;
            joined += ib.substr(eol + 2, n);
            at = eol + 2 + n + 2;
        }
        ib = joined;
    }
    const size_t up = ib.find("\"udp\":");
    if (up != std::string::npos) ib = ib.substr(0, up) + "\"udp\":P}";
    out += "info: " + ib + "\n";
    const std::string nf = httpGet(s.port, "/index.html", s);
    out += "no static: " + std::string(nf.find(" 404 ") != std::string::npos ? "404 " : "?") +
           (nf.find("signalling server: WebSocket at /net") != std::string::npos ? "signalling server: WebSocket at /net" : "?") + "\n";
    uint16_t mine = 0;
    const std::string echo = udpEcho(s.port, s, &mine);
    out += "udp echo: " + std::string(echo == strf("434349570204%04x7f000001", mine) ? "127.0.0.1:own port" : echo) + "\n";
    return out;
}

void serverParityChecks(const Expect& expect, const std::string& buildDir) {
    ServerUnderTest e;
    e.engine = std::make_unique<net::HostServer>();
    net::HostServerConfig c;
    c.port = 0;
    c.testIds = 1001;
    c.quiet = true;
    std::string err;
    if (!e.engine->start(c, &err)) {
        expect(false, "the engine's server starts: " + err);
        return;
    }
    e.port = e.engine->port();
    const std::string te = serverScript(e);
    e.engine->stop();
    std::FILE* f = std::fopen((buildDir + "/net_server_script.txt").c_str(), "w");
    if (f) {
        std::fputs(te.c_str(), f);
        std::fclose(f);
    }
    expect(te.find("\"reason\":\"refused: password\"") != std::string::npos && te.find("refused: version") != std::string::npos &&
               te.find("refused: no such game") != std::string::npos && te.find("refused: full") != std::string::npos &&
               te.find("refused: sealed") != std::string::npos && te.find("\"t\":\"closed\",\"reason\":\"kicked\"") != std::string::npos &&
               te.find("bin ea030000") == std::string::npos && te.find("bin e9030000010109000000ab") != std::string::npos &&
               te.find("bin e903000000" "00cd") != std::string::npos,
           "the engine's server: refusals (password, version, no such game, full, sealed), kick, the relay with the sender's id");
    if (!haveNode()) {
        expect(true, "Node not found: the comparison with server/signal.js skipped");
        return;
    }
    ServerUnderTest n;
    if (!n.node.start("server/signal.js", buildDir + "/net_node_script.log", {}, &err)) {
        expect(false, "server/signal.js starts: " + err);
        return;
    }
    n.port = n.node.port;
    const std::string tn = serverScript(n);
    n.node.stop();
    f = std::fopen((buildDir + "/net_server_script_node.txt").c_str(), "w");
    if (f) {
        std::fputs(tn.c_str(), f);
        std::fclose(f);
    }
    size_t lines = 0;
    for (char ch : te) lines += ch == '\n';
    expect(te == tn, strf("the engine's server and server/signal.js answer a script of %zu messages byte for byte alike "
                          "(build/net_server_script*.txt)",
                          lines));
}

// Two native transports and the engine's server, a sim clock: the guaranteed channel over a lossy
// direct path, its fallback to the relay when the path dies, and the path coming back.
void reliableChannelChecks(const Expect& expect) {
    net::HostServer server;
    net::HostServerConfig c;
    c.port = 0;
    c.testIds = 1001;
    c.quiet = true;
    std::string err;
    if (!server.start(c, &err)) {
        expect(false, "server: " + err);
        return;
    }
    const std::string url = "ws://127.0.0.1:" + std::to_string(server.port()) + "/net";
    double now = 0;
    auto make = [&](uint32_t seed) {
        net::NativeNetConfig cfg;
        cfg.server = url;
        cfg.link.lan = false;
        cfg.link.faults.relLoss = 0.3f;
        cfg.link.faults.unrelLoss = 0.3f;
        cfg.link.faults.jitter = 0.05;
        cfg.link.faults.seed = seed;
        auto t = std::make_unique<net::NativeTransport>(cfg);
        t->setClock([&now]() { return now; });
        return t;
    };
    auto A = make(11), B = make(23);
    std::vector<net::TransportEvent> evA, evB;
    auto pump = [&]() {
        for (int k = 0; k < 4; ++k) {
            server.pump(0);
            net::TransportEvent ev;
            while (A->poll(ev)) evA.push_back(ev);
            while (B->poll(ev)) evB.push_back(ev);
        }
    };
    net::SessionDesc d;
    d.version = net::kGameVersion;
    A->host(d);
    for (int i = 0; i < 200 && A->code().empty(); ++i) {
        pump();
        server.pump(1);
    }
    B->join(A->code(), "", net::kGameVersion);
    for (int i = 0; i < 200 && !B->self(); ++i) {
        pump();
        server.pump(1);
    }
    for (int i = 0; i < 60 && !(A->links().size() == 1 && A->links()[0].direct && B->links().size() == 1 && B->links()[0].direct); ++i) {
        now += 1.0 / 60.0;
        pump();
        server.pump(1);
    }
    const bool upBoth = A->links().size() == 1 && A->links()[0].direct && B->links().size() == 1 && B->links()[0].direct;
    expect(upBoth && A->links()[0].native && B->links()[0].native,
           strf("two native players found the direct path (%s ↔ %s)", upBoth ? B->links()[0].path.c_str() : "-",
                upBoth ? A->links()[0].path.c_str() : "-"));
    const net::PlayerId a = A->self(), b = B->self();
    evA.clear();
    evB.clear();
    // 300 frames: 2 guaranteed and 1 unguaranteed message each way per frame, 30 % lost, up to
    // 50 ms extra delay on the unguaranteed ones.
    uint32_t nextRel = 0, nextUnrel = 0;
    auto sendSome = [&](int frames) {
        for (int f = 0; f < frames; ++f) {
            for (int k = 0; k < 2; ++k) {
                const uint32_t v = nextRel++;
                const uint8_t m[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
                A->send(b, true, m, 4);
                B->send(a, true, m, 4);
            }
            const uint32_t u = nextUnrel++;
            const uint8_t m[4] = {uint8_t(u), uint8_t(u >> 8), uint8_t(u >> 16), uint8_t(u >> 24)};
            A->send(b, false, m, 4);
            B->send(a, false, m, 4);
            now += 1.0 / 60.0;
            pump();
        }
    };
    auto check = [&](const std::vector<net::TransportEvent>& ev, uint32_t upTo, int& unrel, int& reordered) {
        uint32_t want = 0;
        bool inOrder = true;
        unrel = 0;
        reordered = 0;
        int lastU = -1;
        for (const net::TransportEvent& e : ev) {
            if (e.kind != net::TransportEvent::Message || e.data.size() != 4) continue;
            const uint32_t v = uint32_t(e.data[0]) | (uint32_t(e.data[1]) << 8) | (uint32_t(e.data[2]) << 16) | (uint32_t(e.data[3]) << 24);
            if (e.reliable) {
                inOrder = inOrder && v == want;
                ++want;
            } else {
                ++unrel;
                if (int(v) < lastU) ++reordered;
                lastU = std::max(lastU, int(v));
            }
        }
        return inOrder && want == upTo;
    };
    sendSome(300);
    for (int i = 0; i < 120; ++i) {  // 2 s for the last resends
        now += 1.0 / 60.0;
        pump();
    }
    int ua, ra, ub, rb;
    const bool okA = check(evA, nextRel, ua, ra), okB = check(evB, nextRel, ub, rb);
    const net::LinkStats& sa = A->directStats();
    expect(okA && okB, strf("%u guaranteed messages each way over a direct path losing 30 %%: all arrived, in order, once (A resent %d)",
                            nextRel, sa.resent));
    expect(ua > 100 && ua < 300 && ub > 100 && ub < 300 && ra + rb > 0,
           strf("unguaranteed: %d and %d of 300 arrived, %d out of order (sent once, delayed up to 50 ms)", ua, ub, ra + rb));
    expect(A->sentDirectTotal > 700 && A->sentRelayTotal < 50,
           strf("the direct path carried them (%d direct, %d relayed)", A->sentDirectTotal, A->sentRelayTotal));
    // The path dies (A's datagrams vanish both ways): the guaranteed messages go over the relay.
    A->directLinks().faults().blackhole = true;
    const int relayedBefore = A->sentRelayTotal;
    sendSome(600);  // 10 s
    for (int i = 0; i < 180; ++i) {
        now += 1.0 / 60.0;
        pump();
    }
    const bool okA2 = check(evA, nextRel, ua, ra), okB2 = check(evB, nextRel, ub, rb);
    expect(okA2 && okB2 && !A->links()[0].direct && !B->links()[0].direct && A->directStats().handedToRelay > 500 &&
               A->sentRelayTotal > relayedBefore,
           strf("the path died: lost after 8 s, %d messages handed to the relay; all %u guaranteed messages still arrived in order",
                A->directStats().handedToRelay, nextRel));
    A->directLinks().faults().blackhole = false;
    for (int i = 0; i < 120 && !(A->links()[0].direct && B->links()[0].direct); ++i) {
        now += 1.0 / 60.0;
        pump();
    }
    expect(A->links()[0].direct && B->links()[0].direct, "the path came back after the probes started again");
    sendSome(60);
    for (int i = 0; i < 120; ++i) {
        now += 1.0 / 60.0;
        pump();
    }
    expect(check(evA, nextRel, ua, ra) && check(evB, nextRel, ub, rb), strf("and %u guaranteed messages in order in the end", nextRel));
    A->leave();
    B->leave();
    for (int i = 0; i < 10; ++i) pump();
    server.stop();
}

// A browser-like client (net.js's framing, caps "webrtc") in a session hosted by a native player:
// the native side sees a browser, reaches it over the relay only, and both read each other.
void browserFramingChecks(const Expect& expect) {
    net::HostServer server;
    net::HostServerConfig c;
    c.port = 0;
    c.testIds = 1001;
    c.quiet = true;
    std::string err;
    if (!server.start(c, &err)) {
        expect(false, "server: " + err);
        return;
    }
    const std::string url = "ws://127.0.0.1:" + std::to_string(server.port()) + "/net";
    net::NativeNetConfig cfg;
    cfg.server = url;
    net::NativeTransport host(cfg);
    net::SessionDesc d;
    host.host(d);
    std::vector<net::TransportEvent> ev;
    auto pump = [&](RawClient* r) {
        for (int k = 0; k < 50; ++k) {
            server.pump(1);
            net::TransportEvent e;
            while (host.poll(e)) ev.push_back(e);
            if (r) {
                r->c.service(realNow());
                r->read();
            }
        }
    };
    pump(nullptr);
    RawClient br;
    br.c.open(url);
    pump(&br);
    br.c.sendText("{\"t\":\"join\",\"code\":\"" + host.code() + "\",\"password\":\"\",\"version\":\"cci-1\",\"caps\":[\"webrtc\"],\"proto\":2}");
    pump(&br);
    bool sawCaps = false;
    for (const std::string& g : br.got)
        if (startsWith(g, "{\"t\":\"opened\"") && g.find("\"1001\":[\"native\",\"udp\"]") != std::string::npos &&
            g.find("\"1002\":[\"webrtc\"]") != std::string::npos)
            sawCaps = true;
    const std::vector<net::PeerLink> l = host.links();
    expect(sawCaps && l.size() == 1 && !l[0].native && !l[0].direct,
           "a browser joins a native host: its 'opened' lists the host as native/udp; the host sees a browser (relay only)");
    const uint8_t m1[] = {7, 7}, m2[] = {8};
    host.send(1002, true, m1, 2);
    host.send(1002, true, m1, 2);
    host.send(1002, false, m2, 1);
    pump(&br);
    std::vector<std::string> bins;
    for (const std::string& g : br.got)
        if (startsWith(g, "bin ")) bins.push_back(g.substr(4));
    expect(bins.size() == 3 && bins[0] == "e903000001" "01010000000707" && bins[1] == "e903000001" "01020000000707" &&
               bins[2] == "e903000000" "0008",
           "the native host's messages reach the browser in net.js's framing: [from][flags][1][seq 1, 2][message], [0][message]");
    // The browser's (net.js) frames to the host, guaranteed ones out of order on the wire.
    const uint8_t b2[] = {0xE9, 0x03, 0, 0, 1, 1, 2, 0, 0, 0, 0xB2};
    const uint8_t b1[] = {0xE9, 0x03, 0, 0, 1, 1, 1, 0, 0, 0, 0xB1};
    const uint8_t bu[] = {0xE9, 0x03, 0, 0, 0, 0, 0xCC};
    br.c.sendBinary(b2, sizeof b2);
    br.c.sendBinary(bu, sizeof bu);
    br.c.sendBinary(b1, sizeof b1);
    br.c.sendBinary(b1, sizeof b1);  // a copy
    pump(&br);
    std::vector<std::string> msgs;
    for (const net::TransportEvent& e : ev)
        if (e.kind == net::TransportEvent::Message) msgs.push_back(strf("%u %s %s", unsigned(e.peer), e.reliable ? "g" : "u", hexOf(e.data).c_str()));
    expect(msgs == std::vector<std::string>({"1002 u cc", "1002 g b1", "1002 g b2"}),
           "the browser's frames reach the native host: guaranteed ones put in order, the copy dropped");
    server.stop();
}

bool haveOpensslCli() { return std::system("openssl version > /dev/null 2>&1") == 0; }

void tlsChecks(const Expect& expect, const std::string& buildDir) {
    if (!haveNode() || !haveOpensslCli()) {
        expect(true, "wss:// checks skipped (no node or no openssl program)");
        return;
    }
    const std::string dir = buildDir + "/net_tls";
    if (std::system(("mkdir -p '" + dir + "'").c_str()) != 0) return;
    const std::string cert = dir + "/cert.pem", key = dir + "/key.pem";
    const std::string cmd = "openssl req -x509 -newkey rsa:2048 -nodes -keyout '" + key + "' -out '" + cert +
                            "' -days 2 -subj /CN=localhost -addext subjectAltName=DNS:localhost,IP:127.0.0.1 > /dev/null 2>&1";
    if (std::system(cmd.c_str()) != 0) {
        expect(false, "a test certificate could be made with openssl");
        return;
    }
    NodeServer n;
    std::string err;
    if (!n.start("server/signal.js", buildDir + "/net_node_tls.log", {"--tls-cert", cert, "--tls-key", key}, &err, "https")) {
        expect(false, "server/signal.js with TLS starts: " + err);
        return;
    }
    auto tryList = [&](const std::string& url, const std::string& ca, std::string& why) {
        net::NativeNetConfig cfg;
        cfg.server = url;
        cfg.direct = false;
        cfg.ws.caFile = ca;
        net::NativeTransport t(cfg);
        t.requestList();
        std::vector<net::SessionInfo> l;
        const double until = realNow() + 10;
        while (realNow() < until) {
            t.service();
            if (t.takeSessionList(l)) return true;
            if (t.state() == net::NativeTransport::State::Failed) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        why = t.error();
        return false;
    };
    const std::string port = std::to_string(n.port);
    std::string why1, why2, why3;
    const bool byName = tryList("wss://localhost:" + port + "/net", cert, why1);
    const bool byIp = tryList("wss://127.0.0.1:" + port + "/net", cert, why2);
    const bool untrusted = tryList("wss://localhost:" + port + "/net", "", why3);
    expect(byName && byIp, "wss://: the game list through TLS, the certificate checked by name (localhost) and by address (127.0.0.1)" +
                               (why1.empty() ? std::string() : " [" + why1 + "]") + (why2.empty() ? std::string() : " [" + why2 + "]"));
    expect(!untrusted && why3.find("certificate") != std::string::npos,
           "wss://: an untrusted certificate is refused (" + why3 + ")");
    // A session over wss: host and join, a message each way (the relay through TLS).
    net::NativeNetConfig cfg;
    cfg.server = "wss://localhost:" + port + "/net";
    cfg.ws.caFile = cert;
    cfg.direct = false;
    net::NativeTransport h(cfg), g(cfg);
    net::SessionDesc d;
    h.host(d);
    std::vector<net::TransportEvent> eh, eg;
    auto pump = [&]() {
        net::TransportEvent e;
        while (h.poll(e)) eh.push_back(e);
        while (g.poll(e)) eg.push_back(e);
    };
    double until = realNow() + 10;
    while (realNow() < until && h.code().empty()) {
        pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    g.join(h.code());
    until = realNow() + 10;
    while (realNow() < until && !g.self()) {
        pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const uint8_t hello[] = {'h', 'i'};
    h.send(g.self(), true, hello, 2);
    g.send(h.self(), true, hello, 2);
    int got = 0;
    until = realNow() + 10;
    while (realNow() < until && got < 2) {
        pump();
        got = 0;
        for (auto* v : {&eh, &eg})
            for (const net::TransportEvent& e : *v) got += e.kind == net::TransportEvent::Message;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    expect(got == 2, "wss://: a session hosted and joined, a guaranteed message each way through the TLS relay");
    h.leave();
    g.leave();
    for (int i = 0; i < 20; ++i) {
        pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void lanChecks(const Expect& expect) {
    // LAN mode answers LAN searches from the local network; 127.0.0.1 mode does not listen there.
    net::HostServer server;
    net::HostServerConfig c;
    c.port = 0;
    c.lan = true;
    c.name = "test host";
    c.quiet = true;
    std::string err;
    if (!server.start(c, &err)) {
        expect(false, "a LAN server starts: " + err);
        return;
    }
    net::NativeNetConfig cfg;
    cfg.server = "ws://127.0.0.1:" + std::to_string(server.port()) + "/net";
    cfg.direct = false;
    net::NativeTransport t(cfg);
    net::SessionDesc d;
    d.name = "LAN game";
    t.host(d);
    for (int i = 0; i < 100 && t.code().empty(); ++i) {
        server.pump(1);
        t.service();
    }
    net::LanSearch search;
    search.start(server.port());
    std::vector<net::LanServer> found;
    for (int i = 0; i < 200 && found.empty(); ++i) {
        server.pump(1);
        search.service(realNow());
        found = search.results();
    }
    const bool ok = !found.empty() && found[0].name == "test host" && found[0].port == server.port() && found[0].sessions.size() == 1 &&
                    found[0].sessions[0].name == "LAN game" && found[0].sessions[0].code == t.code();
    expect(ok, strf("a LAN search finds the LAN-mode server (%s) and its game %s", found.empty() ? "nothing" : found[0].url.c_str(),
                    t.code().c_str()));
    const std::vector<std::string> urls = server.urls();
    expect(!urls.empty() && urls.back() == "ws://127.0.0.1:" + std::to_string(server.port()) + "/net",
           strf("LAN mode lists %zu join addresses (each LAN address and 127.0.0.1)", urls.size()));
    server.stop();
}

// "Host on this computer": the server on its own thread, the host's transport and a joiner on
// this one.
void threadChecks(const Expect& expect) {
    net::HostServer server;
    net::HostServerConfig c;
    c.port = 0;
    c.quiet = true;
    std::string err;
    if (!server.start(c, &err) || !server.startThread()) {
        expect(false, "a server on its own thread starts: " + err);
        return;
    }
    const std::string url = "ws://127.0.0.1:" + std::to_string(server.port()) + "/net";
    net::NativeNetConfig cfg;
    cfg.server = url;
    net::NativeTransport h(cfg), g(cfg);
    net::SessionDesc d;
    d.name = "on a thread";
    h.host(d);
    std::vector<net::TransportEvent> eh, eg;
    auto pump = [&]() {
        net::TransportEvent e;
        while (h.poll(e)) eh.push_back(e);
        while (g.poll(e)) eg.push_back(e);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    };
    for (double until = realNow() + 5; realNow() < until && h.code().empty();) pump();
    g.join(h.code());
    for (double until = realNow() + 5; realNow() < until && !(g.self() && !g.links().empty() && g.links()[0].direct);) pump();
    const std::vector<net::HostedSession> ss = server.sessions();
    const uint8_t m[] = {42};
    h.send(g.self(), true, m, 1);
    bool got = false;
    for (double until = realNow() + 5; realNow() < until && !got;) {
        pump();
        for (const net::TransportEvent& e : eg) got = got || (e.kind == net::TransportEvent::Message && e.data == std::vector<uint8_t>{42});
    }
    expect(ss.size() == 1 && ss[0].name == "on a thread" && ss[0].order.size() == 2 && server.connections() == 2 && got &&
               !g.links().empty() && g.links()[0].direct,
           "the server on its own thread: a session hosted and joined, the direct path up, a message across; its state read from here");
    h.leave();
    g.leave();
    for (int i = 0; i < 20; ++i) pump();
    server.stop();
    expect(!server.running() && server.connections() == 0, "the threaded server stops cleanly");
}

}  // namespace

bool haveNode() {
    static int have = -1;
    if (have < 0) have = std::system("node --version > /dev/null 2>&1") == 0 ? 1 : 0;
    return have == 1;
}

std::unique_ptr<RigNet> makeSocketNet(const SocketNetOptions& o, std::string* error) {
    auto n = std::make_unique<SocketNet>(o);
    if (!n->start(error)) return nullptr;
    return n;
}

void testNetSocketUnits(const Expect& expect, const std::string& buildDir) {
    std::printf("network sockets: JSON, WebSocket codec\n");
    jsonChecks(expect);
    wsCodecChecks(expect);
    std::printf("network sockets: the two servers\n");
    serverParityChecks(expect, buildDir);
    std::printf("network sockets: the guaranteed channel over a lossy direct path\n");
    reliableChannelChecks(expect);
    std::printf("network sockets: a browser's framing\n");
    browserFramingChecks(expect);
    std::printf("network sockets: wss://\n");
    tlsChecks(expect, buildDir);
    std::printf("network sockets: LAN\n");
    lanChecks(expect);
    std::printf("network sockets: the server on its own thread\n");
    threadChecks(expect);
}

}  // namespace cc
