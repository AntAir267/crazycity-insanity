#include "platform/host_server.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>
#include <random>
#include <thread>

#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "core/js_value.h"
#include "core/strutil.h"
#include "platform/native_transport.h"
#include "platform/net_socket.h"
#include "platform/websocket.h"

namespace cc {
namespace net {

namespace {

constexpr size_t kMaxHeader = 16384;
constexpr size_t kMaxQueued = 32u << 20;  // a connection that stops reading is closed
const uint8_t kEcho[4] = {'C', 'C', 'I', 'W'};
const uint8_t kSearch[4] = {'C', 'C', 'I', 'L'};

std::string clockText() {
    // signal.js: new Date().toISOString().slice(11, 19) (UTC).
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char b[16];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return b;
}

const char* contentType(const std::string& path) {
    // signal.js's table; everything else application/octet-stream.
    static const struct {
        const char* ext;
        const char* type;
    } T[] = {{".html", "text/html; charset=utf-8"}, {".js", "text/javascript"}, {".css", "text/css"},
             {".json", "application/json"},         {".wasm", "application/wasm"}, {".png", "image/png"},
             {".jpg", "image/jpeg"},                 {".svg", "image/svg+xml"}};
    const size_t dot = path.rfind('.');
    const size_t slash = path.rfind('/');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "application/octet-stream";
    const std::string ext = toLower(path.substr(dot));
    for (const auto& t : T)
        if (ext == t.ext) return t.type;
    return "application/octet-stream";
}

// decodeURIComponent (false on a bad escape, as it throws).
bool percentDecode(const std::string& s, std::string& out) {
    out.clear();
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '%') {
            out += s[i];
            continue;
        }
        if (i + 2 >= s.size()) return false;
        auto hex = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
        const int a = hex(s[i + 1]), b = hex(s[i + 2]);
        if (a < 0 || b < 0) return false;
        out += char(a * 16 + b);
        i += 2;
    }
    return true;
}

// The cleaned path of a request below the static root ("" when it leaves the root).
bool safeRelative(const std::string& rel, std::string& out) {
    std::vector<std::string> parts;
    for (const std::string& p : split(rel, '/')) {
        if (p.empty() || p == ".") continue;
        if (p == "..") {
            if (parts.empty()) return false;
            parts.pop_back();
            continue;
        }
        if (p.find('\0') != std::string::npos || p.find('\\') != std::string::npos) return false;
        parts.push_back(p);
    }
    out.clear();
    for (const std::string& p : parts) out += "/" + p;
    return true;
}

std::string hostName() {
#ifndef _WIN32
    char b[256] = {0};
    if (gethostname(b, sizeof b - 1) == 0 && b[0]) return b;
#endif
    return "CrazyCity host";
}

}  // namespace

struct HostServer::Impl {
    struct Conn;
    struct Desc {
        std::string name = "Game", password, city, version;
        double maxPlayers = 8, mode = 0;
        bool sealed = false;
    };
    struct Session {
        std::string code;
        Desc desc;
        std::map<PlayerId, Conn*> players;
        std::vector<PlayerId> order;
        PlayerId host = 0;
    };
    struct Conn {
        uint64_t serial = 0;
        sock::Fd fd = sock::kNoFd;
        sock::Addr addr;
        bool ws = false;
        bool dead = false;
        bool closeWhenSent = false;
        std::string in;
        ws::Decoder dec;
        ws::OutQueue out;
        std::FILE* file = nullptr;
        uint64_t fileLeft = 0;
        bool keepAlive = true;
        PlayerId id = 0;
        Session* session = nullptr;
        JsValue caps;
        ~Conn() {
            if (file) std::fclose(file);
            if (fd != sock::kNoFd) sock::closeFd(fd);
        }
    };

    HostServerConfig cfg;
    sock::Fd listener = sock::kNoFd, udp = sock::kNoFd;
    uint16_t boundPort = 0;
    std::string bindHost;
    std::map<uint64_t, std::unique_ptr<Conn>> conns;
    uint64_t nextSerial = 1;
    std::map<std::string, std::unique_ptr<Session>> sessions;
    std::map<PlayerId, bool> usedIds;
    std::mt19937 rng{std::random_device{}()};
    uint32_t nextTestId = 0;
    uint32_t nextTestCode = 0;
    mutable std::mutex mu;
    std::thread thread;
    std::atomic<bool> stopFlag{false};
    bool started = false;

    void log(const std::string& line) {
        if (cfg.quiet) return;
        if (cfg.log) {
            cfg.log(line);
            return;
        }
        std::printf("%s %s\n", clockText().c_str(), line.c_str());
        std::fflush(stdout);
    }

    // ---- sending

    void write(Conn& c) {
        if (c.dead || c.fd == sock::kNoFd) return;
        if (!c.out.flush([&](const uint8_t* p, size_t n) -> long {
                const long r = sock::send(c.fd, p, n);
                if (r == sock::kWouldBlock) return 0;
                return r < 0 ? -1 : r;
            })) {
            c.dead = true;
            return;
        }
        if (c.out.bytes() > kMaxQueued) c.dead = true;
    }
    void sendRaw(Conn& c, const std::string& s) {
        c.out.push(std::vector<uint8_t>(s.begin(), s.end()));
        write(c);
    }
    void sendFrame(Conn& c, ws::Op op, const uint8_t* p, size_t n, bool droppable = false, uint32_t key = 0) {
        if (c.dead) return;
        std::vector<uint8_t> f;
        ws::encodeFrame(f, op, p, n, false);
        c.out.push(std::move(f), droppable, key);
        write(c);
    }
    void sendJson(Conn& c, const JsValue& v) {
        const std::string s = v.dump();
        sendFrame(c, ws::Text, reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }

    // ---- sessions (signal.js)

    PlayerId newId() {
        if (cfg.testIds) {
            for (;;) {
                const PlayerId id = nextTestId++;
                if (!usedIds.count(id)) {
                    usedIds[id] = true;
                    return id;
                }
            }
        }
        std::uniform_int_distribution<uint32_t> d(1000u, 0x7ffffffeu);
        for (;;) {
            const PlayerId id = d(rng);
            if (!usedIds.count(id)) {
                usedIds[id] = true;
                return id;
            }
        }
    }
    std::string newCode() {
        static const char* A = "ABCDEFGHJKLMNPQRSTUVWXYZ";
        for (;;) {
            std::string c;
            if (cfg.testIds) {
                uint32_t n = nextTestCode++;
                char b[4];
                for (int i = 3; i >= 0; --i) {
                    b[i] = A[n % 24];
                    n /= 24;
                }
                c.assign(b, 4);
            } else {
                std::uniform_int_distribution<int> d(0, 23);
                for (int i = 0; i < 4; ++i) c += A[d(rng)];
            }
            if (!sessions.count(c)) return c;
        }
    }

    static Desc cleanDesc(const JsValue& d, const Desc* old) {
        // signal.js cleanDesc: the same JavaScript conversions.
        Desc r;
        const JsValue& name = d["name"];
        r.name = utf16Slice(name.truthy() ? name.jsString() : (old && !old->name.empty()) ? old->name : std::string("Game"), 40);
        const JsValue& pw = d["password"];
        r.password = utf16Slice(!pw.isUndefined() ? pw.jsString() : old ? old->password : std::string(), 40);
        const JsValue& mp = d["maxPlayers"];
        double m;
        if (mp.truthy()) m = mp.jsNumber();
        else if (old && old->maxPlayers != 0 && !std::isnan(old->maxPlayers)) m = old->maxPlayers;
        else m = 8;
        r.maxPlayers = std::isnan(m) ? m : std::max(1.0, std::min(8.0, m));
        r.sealed = d["sealed"].truthy();
        r.city = utf16Slice(d["city"].truthy() ? d["city"].jsString() : std::string(), 40);
        r.mode = d["mode"].truthy() ? d["mode"].jsNumber() : 0.0;
        const JsValue& v = d["version"];
        r.version = utf16Slice(!v.isUndefined() ? v.jsString() : old ? old->version : std::string(), 40);
        return r;
    }

    static JsValue publicDesc(const Session& s) {
        JsValue o = JsValue::object();
        o.set("code", s.code);
        o.set("name", s.desc.name.empty() ? std::string("Game") : s.desc.name);
        o.set("players", int(s.order.size()));
        o.set("max", s.desc.maxPlayers);
        o.set("sealed", s.desc.sealed);
        o.set("city", s.desc.city);
        o.set("mode", (s.desc.mode != 0 && !std::isnan(s.desc.mode)) ? s.desc.mode : 0.0);
        o.set("password", !s.desc.password.empty());
        o.set("version", s.desc.version);
        return o;
    }

    static JsValue cleanCaps(const JsValue& c) {
        // What the machine says it can do; a page without the field is a browser (WebRTC).
        JsValue out = JsValue::array();
        if (!c.isArray()) {
            out.push("webrtc");
            return out;
        }
        for (const JsValue& x : c.items()) {
            if (out.size() >= 8) break;
            if (x.isString() && !x.str().empty()) out.push(utf16Slice(x.str(), 16));
        }
        return out;
    }

    JsValue rosterCaps(const Session& s) {
        JsValue o = JsValue::object();
        for (PlayerId id : s.order) {
            auto it = s.players.find(id);
            if (it != s.players.end()) o.set(std::to_string(id), it->second->caps);
        }
        return o;
    }

    JsValue rosterIds(const Session& s) {
        JsValue r = JsValue::array();
        for (PlayerId id : s.order) r.push((long long)id);
        return r;
    }

    void removePlayer(Session* s, PlayerId id, const std::string& reason, bool notifySelf) {
        auto it = s->players.find(id);
        if (it == s->players.end()) return;
        Conn* p = it->second;
        s->players.erase(it);
        s->order.erase(std::remove(s->order.begin(), s->order.end(), id), s->order.end());
        usedIds.erase(id);
        p->session = nullptr;
        if (notifySelf && !p->dead) {
            JsValue m = JsValue::object();
            m.set("t", "closed");
            m.set("reason", reason);
            sendJson(*p, m);
        }
        for (auto& kv : s->players) {
            JsValue m = JsValue::object();
            m.set("t", "left");
            m.set("peer", (long long)id);
            sendJson(*kv.second, m);
        }
        if (s->host == id) {
            s->host = s->order.empty() ? 0 : s->order.front();
            if (s->host)
                for (auto& kv : s->players) {
                    JsValue m = JsValue::object();
                    m.set("t", "host");
                    m.set("host", (long long)s->host);
                    sendJson(*kv.second, m);
                }
        }
        log(strf("session %s: player %u out (%s); %zu left", s->code.c_str(), unsigned(id), reason.c_str(), s->order.size()));
        if (s->order.empty()) {
            const std::string code = s->code;
            sessions.erase(code);
            log(strf("session %s closed", code.c_str()));
        }
    }

    void onBinary(Conn& me, const std::vector<uint8_t>& data) {
        // Relay: [u32 to][u8 flags][payload] → [u32 from][u8 flags][payload]. An unguaranteed
        // one (flags bit 0 clear) is latest-wins while the receiver's socket is backed up.
        Session* s = me.session;
        if (!s || data.size() < 5) return;
        const PlayerId to = PlayerId(data[0]) | (PlayerId(data[1]) << 8) | (PlayerId(data[2]) << 16) | (PlayerId(data[3]) << 24);
        auto it = s->players.find(to);
        if (it == s->players.end()) return;
        std::vector<uint8_t> out(data);
        for (int i = 0; i < 4; ++i) out[size_t(i)] = uint8_t(me.id >> (8 * i));
        const bool guaranteed = (data[4] & 1) != 0;
        sendFrame(*it->second, ws::Binary, out.data(), out.size(), !guaranteed, me.id);
    }

    void onText(Conn& me, const std::string& text) {
        JsValue m;
        if (!JsValue::parse(text, m)) return;
        const std::string t = m["t"].isString() ? m["t"].str() : std::string();
        if (t == "list") {
            JsValue l = JsValue::array();
            for (auto& kv : sessions) l.push(publicDesc(*kv.second));
            JsValue r = JsValue::object();
            r.set("t", "sessions");
            r.set("list", l);
            sendJson(me, r);
            return;
        }
        if (t == "ping") {
            JsValue r = JsValue::object();
            r.set("t", "pong");
            r.set("n", m["n"]);
            sendJson(me, r);
            return;
        }
        if (t == "host") {
            if (me.session) return;
            auto s = std::make_unique<Session>();
            s->code = newCode();
            s->desc = cleanDesc(m["desc"], nullptr);
            me.id = newId();
            me.caps = cleanCaps(m["caps"]);
            s->players[me.id] = &me;
            s->order.push_back(me.id);
            s->host = me.id;
            me.session = s.get();
            Session* raw = s.get();
            sessions[s->code] = std::move(s);
            JsValue r = JsValue::object();
            r.set("t", "opened");
            r.set("self", (long long)me.id);
            r.set("host", (long long)me.id);
            r.set("roster", rosterIds(*raw));
            r.set("code", raw->code);
            r.set("caps", rosterCaps(*raw));
            sendJson(me, r);
            log(strf("session %s hosted by %u (%s)", raw->code.c_str(), unsigned(me.id), me.addr.host().c_str()));
            return;
        }
        if (t == "join") {
            if (me.session) return;
            std::string code = m["code"].truthy() ? m["code"].jsString() : std::string();
            for (char& c : code)
                if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
            auto it = sessions.find(code);
            Session* s = it == sessions.end() ? nullptr : it->second.get();
            std::string why;
            if (!s) why = "no such game";
            else if (s->desc.sealed) why = "sealed";
            else if (double(s->order.size()) >= s->desc.maxPlayers) why = "full";
            else if (!s->desc.password.empty() && (m["password"].truthy() ? m["password"].jsString() : std::string()) != s->desc.password)
                why = "password";
            else if (!s->desc.version.empty() && m["version"].truthy() && m["version"].jsString() != s->desc.version)
                why = "version";
            if (!why.empty()) {
                JsValue r = JsValue::object();
                r.set("t", "closed");
                r.set("reason", "refused: " + why);
                sendJson(me, r);
                return;
            }
            me.id = newId();
            me.caps = cleanCaps(m["caps"]);
            s->players[me.id] = &me;
            s->order.push_back(me.id);
            me.session = s;
            JsValue r = JsValue::object();
            r.set("t", "opened");
            r.set("self", (long long)me.id);
            r.set("host", (long long)s->host);
            r.set("roster", rosterIds(*s));
            r.set("code", s->code);
            r.set("caps", rosterCaps(*s));
            sendJson(me, r);
            for (auto& kv : s->players) {
                if (kv.second == &me) continue;
                JsValue j = JsValue::object();
                j.set("t", "joined");
                j.set("peer", (long long)me.id);
                j.set("caps", me.caps);
                sendJson(*kv.second, j);
            }
            log(strf("session %s: %u joined (%s); %zu players", s->code.c_str(), unsigned(me.id), me.addr.host().c_str(), s->order.size()));
            return;
        }
        if (t == "describe") {
            Session* s = me.session;
            if (!s || s->host != me.id) return;
            s->desc = cleanDesc(m["desc"], &s->desc);
            return;
        }
        if (t == "kick") {
            Session* s = me.session;
            if (!s || s->host != me.id) return;
            if (m["id"].isNumber() && m["id"].num() == double(me.id)) return;
            const double want = m["id"].jsNumber();
            if (std::isnan(want)) return;
            auto it = s->players.find(PlayerId(want));
            if (it != s->players.end() && double(it->first) == want) removePlayer(s, it->first, "kicked", true);
            return;
        }
        if (t == "leave") {
            if (me.session) removePlayer(me.session, me.id, "left", false);
            return;
        }
        if (t == "signal") {
            Session* s = me.session;
            if (!s) return;
            const double to = m["to"].jsNumber();
            if (std::isnan(to)) return;
            auto it = s->players.find(PlayerId(to));
            if (it == s->players.end() || double(it->first) != to) return;
            JsValue r = JsValue::object();
            r.set("t", "signal");
            r.set("from", (long long)me.id);
            r.set("data", m["data"]);
            sendJson(*it->second, r);
            return;
        }
    }

    void onWsClose(Conn& c) {
        if (c.session) removePlayer(c.session, c.id, "lost", false);
    }

    // ---- HTTP

    void respond(Conn& c, int status, const char* reason, const std::vector<std::pair<std::string, std::string>>& headers,
                 const std::string& body, bool headOnly = false) {
        std::string r = strf("HTTP/1.1 %d %s\r\n", status, reason);
        bool haveLength = false;
        for (const auto& h : headers) {
            r += h.first + ": " + h.second + "\r\n";
            if (iequals(h.first, "Content-Length")) haveLength = true;
        }
        if (!haveLength) r += strf("Content-Length: %zu\r\n", body.size());
        r += c.keepAlive ? "Connection: keep-alive\r\n\r\n" : "Connection: close\r\n\r\n";
        if (!headOnly) r += body;
        sendRaw(c, r);
        if (!c.keepAlive) c.closeWhenSent = true;
    }

    void serveRequest(Conn& c, const std::string& method, const std::string& target, const std::map<std::string, std::string>& hd) {
        std::string pathname = target;
        const size_t q = pathname.find_first_of("?#");
        if (q != std::string::npos) pathname = pathname.substr(0, q);
        if (!startsWith(pathname, "/")) {
            // An absolute address (a proxy request): its path only.
            const size_t s = pathname.find("://");
            const size_t p = s == std::string::npos ? std::string::npos : pathname.find('/', s + 3);
            pathname = p == std::string::npos ? "/" : pathname.substr(p);
        }
        if (pathname == "/net/info") {
            JsValue o = JsValue::object();
            o.set("cci", true);
            o.set("sessions", int(sessions.size()));
            o.set("proto", kNetProtocol);
            o.set("udp", udp != sock::kNoFd ? int(boundPort) : 0);
            respond(c, 200, "OK", {{"Content-Type", "application/json"}, {"Cache-Control", "no-cache"}}, o.dump());
            return;
        }
        if (cfg.staticDir.empty()) {
            respond(c, 404, "Not Found", {}, "signalling server: WebSocket at /net");
            return;
        }
        std::string rel;
        if (!percentDecode(pathname, rel)) {
            respond(c, 400, "Bad Request", {}, "");
            return;
        }
        if (endsWith(rel, "/")) rel += "index.html";
        std::string clean;
        if (!safeRelative(rel, clean)) {
            respond(c, 403, "Forbidden", {}, "");
            return;
        }
        const std::string file = cfg.staticDir + clean;
        struct stat st {};
        if (::stat(file.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
            respond(c, 404, "Not Found", {}, "not found");
            return;
        }
        const uint64_t size = uint64_t(st.st_size);
        std::vector<std::pair<std::string, std::string>> head = {
            {"Content-Type", contentType(file)}, {"Accept-Ranges", "bytes"}, {"Cache-Control", "no-cache"}};
        uint64_t start = 0, end = size ? size - 1 : 0;
        bool partial = false;
        auto rit = hd.find("range");
        if (rit != hd.end()) {
            // /^bytes=(\d*)-(\d*)$/ as signal.js reads it.
            const std::string& r = rit->second;
            if (startsWith(r, "bytes=")) {
                const std::string spec = r.substr(6);
                const size_t dash = spec.find('-');
                bool ok = dash != std::string::npos;
                const std::string a = ok ? spec.substr(0, dash) : std::string(), b = ok ? spec.substr(dash + 1) : std::string();
                for (char ch : a) ok = ok && ch >= '0' && ch <= '9';
                for (char ch : b) ok = ok && ch >= '0' && ch <= '9';
                if (ok) {
                    long long s0, e0;
                    if (a.empty()) {
                        s0 = std::max(0LL, (long long)size - (b.empty() ? 0LL : std::atoll(b.c_str())));
                        e0 = (long long)size - 1;
                    } else {
                        s0 = std::atoll(a.c_str());
                        e0 = b.empty() ? (long long)size - 1 : std::atoll(b.c_str());
                    }
                    e0 = std::min(e0, (long long)size - 1);
                    if (s0 > e0) {
                        respond(c, 416, "Range Not Satisfiable", {{"Content-Range", strf("bytes */%llu", (unsigned long long)size)}}, "");
                        return;
                    }
                    start = uint64_t(s0);
                    end = uint64_t(e0);
                    partial = true;
                }
            }
        }
        const bool headOnly = method == "HEAD";
        const uint64_t len = size == 0 ? 0 : end - start + 1;
        std::FILE* f = nullptr;
        if (!headOnly && len > 0) {
            f = std::fopen(file.c_str(), "rb");
            if (!f || std::fseek(f, long(start), SEEK_SET) != 0) {
                if (f) std::fclose(f);
                respond(c, 404, "Not Found", {}, "not found");
                return;
            }
        }
        head.push_back({"Content-Length", std::to_string(len)});
        if (partial) {
            head.push_back({"Content-Range", strf("bytes %llu-%llu/%llu", (unsigned long long)start, (unsigned long long)end, (unsigned long long)size)});
            respond(c, 206, "Partial Content", head, "", true);
        } else {
            respond(c, 200, "OK", head, "", true);
        }
        if (f) {
            c.file = f;
            c.fileLeft = len;
            c.closeWhenSent = false;  // after the file
        }
        streamFile(c);
    }

    void streamFile(Conn& c) {
        while (c.file && c.out.bytes() < (256u << 10) && !c.dead) {
            std::vector<uint8_t> chunk(size_t(std::min<uint64_t>(c.fileLeft, 65536)));
            const size_t n = std::fread(chunk.data(), 1, chunk.size(), c.file);
            if (n == 0) {
                c.dead = true;  // the file shrank: the response cannot be finished
                break;
            }
            chunk.resize(n);
            c.fileLeft -= n;
            c.out.push(std::move(chunk));
            if (c.fileLeft == 0) {
                std::fclose(c.file);
                c.file = nullptr;
                if (!c.keepAlive) c.closeWhenSent = true;
            }
            write(c);
            if (c.out.bytes() > 0) break;  // the socket is full: more when it can take it
        }
    }

    void onHttp(Conn& c) {
        while (!c.dead && !c.ws && !c.file && !c.closeWhenSent) {
            const size_t end = c.in.find("\r\n\r\n");
            if (end == std::string::npos) {
                if (c.in.size() > kMaxHeader) c.dead = true;
                return;
            }
            const std::string head = c.in.substr(0, end);
            std::vector<std::string> lines = split(head, '\n');
            for (std::string& l : lines)
                if (!l.empty() && l.back() == '\r') l.pop_back();
            std::map<std::string, std::string> hd;
            for (size_t i = 1; i < lines.size(); ++i) {
                const size_t colon = lines[i].find(':');
                if (colon == std::string::npos) continue;
                hd[toLower(trim(lines[i].substr(0, colon)))] = trim(lines[i].substr(colon + 1));
            }
            size_t bodyLen = 0;
            if (hd.count("content-length")) bodyLen = size_t(std::atoll(hd["content-length"].c_str()));
            if (c.in.size() < end + 4 + bodyLen) {
                if (bodyLen > (1u << 20)) c.dead = true;
                return;
            }
            c.in.erase(0, end + 4 + bodyLen);
            const std::vector<std::string> rl = split(lines.empty() ? std::string() : lines[0], ' ');
            if (rl.size() < 3) {
                c.dead = true;
                return;
            }
            const std::string method = rl[0], target = rl[1], version = rl[2];
            const std::string conn = toLower(hd.count("connection") ? hd["connection"] : std::string());
            c.keepAlive = version == "HTTP/1.1" ? conn.find("close") == std::string::npos : conn.find("keep-alive") != std::string::npos;
            const bool upgrade = conn.find("upgrade") != std::string::npos && hd.count("upgrade");
            if (upgrade) {
                // signal.js: only /net with a key; anything else is dropped.
                std::string path = target;
                const size_t q = path.find_first_of("?#");
                if (q != std::string::npos) path = path.substr(0, q);
                const auto key = hd.find("sec-websocket-key");
                if (path != "/net" || key == hd.end() || key->second.empty()) {
                    c.dead = true;
                    return;
                }
                sendRaw(c, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
                               ws::acceptKey(key->second) + "\r\n\r\n");
                c.ws = true;
                // Proto 2: what this server speaks and its UDP port (the address answer).
                JsValue w = JsValue::object();
                w.set("t", "welcome");
                w.set("proto", kNetProtocol);
                w.set("udp", udp != sock::kNoFd ? int(boundPort) : 0);
                sendJson(c, w);
                const std::string rest = c.in;
                c.in.clear();
                if (!rest.empty()) onWsBytes(c, reinterpret_cast<const uint8_t*>(rest.data()), rest.size());
                return;
            }
            if (method != "GET" && method != "HEAD") {
                respond(c, 405, "Method Not Allowed", {}, "");
                continue;
            }
            serveRequest(c, method, target, hd);
        }
    }

    void onWsBytes(Conn& c, const uint8_t* d, size_t n) {
        std::vector<ws::Decoder::Message> ms;
        if (!c.dec.feed(d, n, ms)) {
            c.dead = true;
            return;
        }
        for (auto& m : ms) {
            if (c.dead) return;
            if (m.op == ws::Close) {
                sendFrame(c, ws::Close, nullptr, 0);
                c.dead = true;
                return;
            }
            if (m.op == ws::Ping) {
                sendFrame(c, ws::Pong, m.data.data(), m.data.size());
                continue;
            }
            if (m.op == ws::Pong) continue;
            if (m.op == ws::Binary) onBinary(c, m.data);
            else onText(c, std::string(m.data.begin(), m.data.end()));
        }
    }

    void readConn(Conn& c) {
        uint8_t buf[65536];
        for (int rounds = 0; rounds < 16 && !c.dead; ++rounds) {
            const long n = sock::recv(c.fd, buf, sizeof buf);
            if (n == sock::kWouldBlock) return;
            if (n <= 0) {
                c.dead = true;
                return;
            }
            if (c.ws) {
                onWsBytes(c, buf, size_t(n));
            } else {
                c.in.append(reinterpret_cast<const char*>(buf), size_t(n));
                onHttp(c);
            }
        }
    }

    // ---- UDP: the address answer and LAN searches

    void onDatagram(const uint8_t* d, size_t n, const sock::Addr& from) {
        if (n >= 28 && std::memcmp(d, kEcho, 4) == 0 && d[4] == 1) {
            // Where this datagram came from (never more bytes back than came in).
            uint8_t r[24] = {'C', 'C', 'I', 'W', 2, 0};
            size_t len;
            if (from.v4()) {
                r[5] = 4;
                r[6] = uint8_t(from.port() >> 8);
                r[7] = uint8_t(from.port());
                const uint32_t ip = from.ipv4();
                std::memcpy(r + 8, &ip, 4);
                len = 12;
            } else {
                r[5] = 6;
                r[6] = uint8_t(from.port() >> 8);
                r[7] = uint8_t(from.port());
                std::memcpy(r + 8, &reinterpret_cast<const sockaddr_in6*>(&from.ss)->sin6_addr, 16);
                len = 24;
            }
            sock::sendTo(udp, r, len, from);
            return;
        }
        if (n >= 8 && std::memcmp(d, kSearch, 4) == 0 && d[4] == 1) {
            // A LAN game search: only in LAN mode and only from the local network.
            if (!cfg.lan || !from.isPrivate()) return;
            JsValue o = JsValue::object();
            o.set("name", cfg.name.empty() ? hostName() : cfg.name);
            o.set("port", int(boundPort));
            o.set("proto", kNetProtocol);
            JsValue l = JsValue::array();
            for (auto& kv : sessions) l.push(publicDesc(*kv.second));
            o.set("sessions", l);
            std::string body = o.dump();
            while (body.size() > 1400 && !o["sessions"].items().empty()) {
                JsValue shorter = JsValue::array();
                const auto& items = o["sessions"].items();
                for (size_t i = 0; i + 1 < items.size(); ++i) shorter.push(items[i]);
                o.set("sessions", shorter);
                o.set("more", true);
                body = o.dump();
            }
            std::vector<uint8_t> r = {'C', 'C', 'I', 'L', 2};
            r.insert(r.end(), body.begin(), body.end());
            sock::sendTo(udp, r.data(), r.size(), from);
        }
    }

    // ---- the loop

    void pumpOnce(int timeoutMs) {
        std::vector<sock::Wait> w;
        std::vector<uint64_t> who;
        {
            std::lock_guard<std::mutex> g(mu);
            if (listener == sock::kNoFd) return;
            w.push_back({listener, sock::kRead, 0});
            who.push_back(0);
            if (udp != sock::kNoFd) {
                w.push_back({udp, sock::kRead, 0});
                who.push_back(0);
            }
            for (auto& kv : conns) {
                Conn& c = *kv.second;
                w.push_back({c.fd, sock::kRead | ((!c.out.empty() || c.file) ? sock::kWrite : 0), 0});
                who.push_back(kv.first);
            }
        }
        sock::wait(w, timeoutMs);
        std::lock_guard<std::mutex> g(mu);
        size_t i = 0;
        if (w[i++].ready & sock::kRead) {
            for (int k = 0; k < 64; ++k) {
                sock::Addr from;
                const sock::Fd fd = sock::tcpAccept(listener, &from);
                if (fd == sock::kNoFd) break;
                if (int(conns.size()) >= cfg.maxConnections) {
                    sock::closeFd(fd);
                    continue;
                }
                auto c = std::make_unique<Conn>();
                c->serial = nextSerial++;
                c->fd = fd;
                c->addr = from;
                conns[c->serial] = std::move(c);
            }
        }
        if (udp != sock::kNoFd) {
            if (w[i++].ready & sock::kRead) {
                uint8_t buf[2048];
                for (int k = 0; k < 256; ++k) {
                    sock::Addr from;
                    const long n = sock::recvFrom(udp, buf, sizeof buf, from);
                    if (n < 0) break;
                    onDatagram(buf, size_t(n), from);
                }
            }
        }
        for (; i < w.size(); ++i) {
            auto it = conns.find(who[i]);
            if (it == conns.end()) continue;
            Conn& c = *it->second;
            if (c.dead) continue;
            if (w[i].ready & sock::kRead) readConn(c);
            if (!c.dead && (w[i].ready & sock::kWrite)) {
                write(c);
                streamFile(c);
                if (!c.ws && !c.file && c.out.empty() && !c.closeWhenSent && !c.in.empty()) onHttp(c);
            }
            if (!c.dead && c.closeWhenSent && c.out.empty() && !c.file) c.dead = true;
        }
        // Closed connections leave their sessions (which can write to others: loop until none).
        for (bool again = true; again;) {
            again = false;
            for (auto it = conns.begin(); it != conns.end();) {
                Conn& c = *it->second;
                if (!c.dead) {
                    ++it;
                    continue;
                }
                if (c.ws) onWsClose(c);
                it = conns.erase(it);
                again = true;
            }
        }
    }
};

HostServer::HostServer() : impl_(std::make_unique<Impl>()) {}

HostServer::~HostServer() { stop(); }

bool HostServer::start(const HostServerConfig& cfg, std::string* error) {
    stop();
    sock::init();
    Impl& m = *impl_;
    m.cfg = cfg;
    m.nextTestId = cfg.testIds;
    m.nextTestCode = 0;
    m.stopFlag = false;
    std::string host = cfg.bind.empty() ? (cfg.lan ? std::string("0.0.0.0") : std::string("127.0.0.1")) : cfg.bind;
    sock::Addr at;
    if (!sock::Addr::parseHost(host, cfg.port, at)) {
        std::vector<sock::Addr> v;
        if (!sock::resolve(host, cfg.port, v, error)) return false;
        at = v.front();
    }
    m.listener = sock::tcpListen(at, 128, error);
    if (m.listener == sock::kNoFd) return false;
    sock::Addr bound;
    sock::localAddr(m.listener, bound);
    m.boundPort = bound.port();
    m.bindHost = host;
    if (cfg.udp) {
        sock::Addr u = at;
        u.setPort(m.boundPort);
        std::string uerr;
        m.udp = sock::udpOpen(u, false, &uerr);
        if (m.udp == sock::kNoFd) m.log("no UDP on port " + std::to_string(m.boundPort) + " (" + uerr + "): native players stay on the relay");
    }
    m.started = true;
    return true;
}

void HostServer::pump(int timeoutMs) { impl_->pumpOnce(timeoutMs); }

bool HostServer::startThread() {
    Impl& m = *impl_;
    if (!m.started || m.thread.joinable()) return false;
    m.stopFlag = false;
    m.thread = std::thread([&m]() {
        while (!m.stopFlag) m.pumpOnce(50);
    });
    return true;
}

void HostServer::stop() {
    Impl& m = *impl_;
    m.stopFlag = true;
    if (m.thread.joinable()) m.thread.join();
    std::lock_guard<std::mutex> g(m.mu);
    m.conns.clear();
    m.sessions.clear();
    m.usedIds.clear();
    if (m.listener != sock::kNoFd) sock::closeFd(m.listener);
    if (m.udp != sock::kNoFd) sock::closeFd(m.udp);
    m.listener = m.udp = sock::kNoFd;
    m.started = false;
}

bool HostServer::running() const { return impl_->started; }

uint16_t HostServer::port() const { return impl_->boundPort; }

std::vector<std::string> HostServer::urls() const {
    const Impl& m = *impl_;
    std::vector<std::string> out;
    if (!m.started) return out;
    const std::string p = std::to_string(m.boundPort);
    if (m.bindHost == "0.0.0.0" || m.bindHost == "::") {
        for (const sock::Addr& a : sock::localIPv4(false)) out.push_back("ws://" + a.host() + ":" + p + "/net");
        out.push_back("ws://127.0.0.1:" + p + "/net");
    } else {
        const bool v6 = m.bindHost.find(':') != std::string::npos;
        out.push_back("ws://" + (v6 ? "[" + m.bindHost + "]" : m.bindHost) + ":" + p + "/net");
    }
    return out;
}

std::vector<HostedSession> HostServer::sessions() const {
    const Impl& m = *impl_;
    std::lock_guard<std::mutex> g(m.mu);
    std::vector<HostedSession> out;
    for (const auto& kv : m.sessions) {
        const Impl::Session& s = *kv.second;
        HostedSession h;
        h.code = s.code;
        h.name = s.desc.name;
        h.password = s.desc.password;
        h.city = s.desc.city;
        h.version = s.desc.version;
        h.maxPlayers = std::isnan(s.desc.maxPlayers) ? 8 : int(s.desc.maxPlayers);
        h.mode = std::isnan(s.desc.mode) ? 0 : int(s.desc.mode);
        h.sealed = s.desc.sealed;
        h.host = s.host;
        h.order = s.order;
        out.push_back(h);
    }
    return out;
}

int HostServer::connections() const {
    std::lock_guard<std::mutex> g(impl_->mu);
    return int(impl_->conns.size());
}

// ---------------------------------------------------------------------------------------------
// Headless

namespace {
std::atomic<bool> g_stop{false};
void onSignal(int) { g_stop = true; }
}  // namespace

int serverMain(const std::vector<std::string>& args) {
    HostServerConfig cfg;
    if (const char* p = std::getenv("PORT")) cfg.port = uint16_t(std::atoi(p));
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto val = [&]() -> std::string { return i + 1 < args.size() ? args[++i] : std::string(); };
        if (a == "--port") cfg.port = uint16_t(std::atoi(val().c_str()));
        else if (a == "--host") cfg.bind = val();
        else if (a == "--lan") cfg.lan = true;
        else if (a == "--static") cfg.staticDir = val();
        else if (a == "--quiet") cfg.quiet = true;
        else if (a == "--no-udp") cfg.udp = false;
        else if (a == "--name") cfg.name = val();
        else if (a == "--test-ids") cfg.testIds = 1001;
        else if (a == "--server") continue;
        else {
            std::fprintf(stderr,
                         "usage: --server [--port 8821] [--host 127.0.0.1 | --lan] [--static dir] [--name text] [--quiet] [--no-udp]\n");
            return 2;
        }
    }
    if (!cfg.staticDir.empty()) {
        while (cfg.staticDir.size() > 1 && cfg.staticDir.back() == '/') cfg.staticDir.pop_back();
#ifndef _WIN32
        char* real = realpath(cfg.staticDir.c_str(), nullptr);
        if (real) {
            cfg.staticDir = real;
            std::free(real);
        }
#endif
    }
    HostServer server;
    std::string err;
    if (!server.start(cfg, &err)) {
        std::fprintf(stderr, "cannot start the network server: %s\n", err.c_str());
        return 1;
    }
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    if (!cfg.quiet) {
        const std::string host = cfg.bind.empty() ? (cfg.lan ? std::string("0.0.0.0") : std::string("127.0.0.1")) : cfg.bind;
        std::printf("%s CrazyCity Insanity network server on http://%s:%u/ (WebSocket /net)%s\n", clockText().c_str(), host.c_str(),
                    unsigned(server.port()), cfg.staticDir.empty() ? "" : (", serving " + cfg.staticDir).c_str());
        if (cfg.lan)
            for (const std::string& u : server.urls()) std::printf("%s   players join at %s\n", clockText().c_str(), u.c_str());
        std::fflush(stdout);
    }
    while (!g_stop) server.pump(250);
    server.stop();
    return 0;
}

}  // namespace net
}  // namespace cc
