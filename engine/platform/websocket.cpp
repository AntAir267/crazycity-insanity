#include "platform/websocket.h"

#include <cstring>
#include <mutex>
#include <random>
#include <thread>
#include <type_traits>

#include "core/strutil.h"

#ifndef CC_NET_NO_TLS
// OpenSSL's headers for its types and numbers only: the library itself is loaded when a wss://
// address is first used (libssl.so.3), so the program has no link-time dependency on it and runs
// where OpenSSL is missing (wss:// then says so).
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

namespace cc {
namespace ws {

// ---------------------------------------------------------------------------------------------
// SHA-1 (FIPS 180-4) and base64, for the handshake only.

namespace {
uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

uint64_t randomBits() {
    thread_local std::mt19937_64 g{std::random_device{}() ^ (uint64_t(std::random_device{}()) << 32)};
    return g();
}
}  // namespace

void sha1(const void* data, size_t size, uint8_t out[20]) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    std::vector<uint8_t> m(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
    const uint64_t bits = uint64_t(size) * 8;
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i = 7; i >= 0; --i) m.push_back(uint8_t(bits >> (8 * i)));
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(m[off + 4 * i]) << 24) | (uint32_t(m[off + 4 * i + 1]) << 16) | (uint32_t(m[off + 4 * i + 2]) << 8) |
                   uint32_t(m[off + 4 * i + 3]);
        for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) f = (b & c) | (~b & d), k = 0x5A827999u;
            else if (i < 40) f = b ^ c ^ d, k = 0x6ED9EBA1u;
            else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDCu;
            else f = b ^ c ^ d, k = 0xCA62C1D6u;
            const uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = t;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 4; ++j) out[4 * i + j] = uint8_t(h[i] >> (24 - 8 * j));
}

std::string base64(const uint8_t* d, size_t n) {
    static const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string s;
    for (size_t i = 0; i < n; i += 3) {
        const uint32_t v = (uint32_t(d[i]) << 16) | (i + 1 < n ? uint32_t(d[i + 1]) << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        s += A[(v >> 18) & 63];
        s += A[(v >> 12) & 63];
        s += i + 1 < n ? A[(v >> 6) & 63] : '=';
        s += i + 2 < n ? A[v & 63] : '=';
    }
    return s;
}

std::string acceptKey(const std::string& key) {
    const std::string k = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    uint8_t h[20];
    sha1(k.data(), k.size(), h);
    return base64(h, 20);
}

std::string newKey() {
    uint8_t b[16];
    for (int i = 0; i < 16; i += 8) {
        const uint64_t r = randomBits();
        std::memcpy(b + i, &r, 8);
    }
    return base64(b, 16);
}

// ---------------------------------------------------------------------------------------------
// Frames

void encodeFrame(std::vector<uint8_t>& out, Op op, const uint8_t* p, size_t n, bool mask) {
    out.push_back(uint8_t(0x80 | op));
    const uint8_t m = mask ? 0x80 : 0;
    if (n < 126) {
        out.push_back(uint8_t(m | n));
    } else if (n < 65536) {
        out.push_back(uint8_t(m | 126));
        out.push_back(uint8_t(n >> 8));
        out.push_back(uint8_t(n));
    } else {
        out.push_back(uint8_t(m | 127));
        for (int i = 7; i >= 0; --i) out.push_back(uint8_t(uint64_t(n) >> (8 * i)));
    }
    if (!mask) {
        out.insert(out.end(), p, p + n);
        return;
    }
    const uint32_t r = uint32_t(randomBits());
    uint8_t k[4] = {uint8_t(r), uint8_t(r >> 8), uint8_t(r >> 16), uint8_t(r >> 24)};
    out.insert(out.end(), k, k + 4);
    const size_t at = out.size();
    out.resize(at + n);
    for (size_t i = 0; i < n; ++i) out[at + i] = p[i] ^ k[i & 3];
}

bool Decoder::feed(const uint8_t* data, size_t size, std::vector<Message>& out) {
    if (at_ > 0 && at_ == buf_.size()) {
        buf_.clear();
        at_ = 0;
    } else if (at_ > 65536 && at_ * 2 > buf_.size()) {
        buf_.erase(buf_.begin(), buf_.begin() + long(at_));
        at_ = 0;
    }
    buf_.insert(buf_.end(), data, data + size);
    for (;;) {
        const size_t have = buf_.size() - at_;
        if (have < 2) return true;
        const uint8_t* b = buf_.data() + at_;
        const bool fin = (b[0] & 0x80) != 0;
        const Op op = Op(b[0] & 15);
        const bool masked = (b[1] & 0x80) != 0;
        uint64_t len = b[1] & 127;
        size_t head = 2;
        if (len == 126) {
            if (have < 4) return true;
            len = (uint64_t(b[2]) << 8) | b[3];
            head = 4;
        } else if (len == 127) {
            if (have < 10) return true;
            len = 0;
            for (int i = 0; i < 8; ++i) len = (len << 8) | b[2 + i];
            head = 10;
        }
        if (len > max_) return false;
        const size_t need = head + (masked ? 4 : 0) + size_t(len);
        if (have < need) return true;
        std::vector<uint8_t> payload(b + head + (masked ? 4 : 0), b + need);
        if (masked) {
            const uint8_t* k = b + head;
            for (size_t i = 0; i < payload.size(); ++i) payload[i] ^= k[i & 3];
        }
        at_ += need;
        if (op >= 8) {
            if (op != Close && op != Ping && op != Pong) return false;
            Message m;
            m.op = op;
            m.data = std::move(payload);
            out.push_back(std::move(m));
            continue;
        }
        if (op == Text || op == Binary) {
            partOp_ = op;
            parts_.clear();
            inMessage_ = true;
        } else if (op != Continuation || !inMessage_) {
            return false;
        }
        if (parts_.size() + payload.size() > max_) return false;
        parts_.insert(parts_.end(), payload.begin(), payload.end());
        if (fin) {
            Message m;
            m.op = partOp_;
            m.data = std::move(parts_);
            parts_.clear();
            inMessage_ = false;
            out.push_back(std::move(m));
        }
    }
}

void OutQueue::push(std::vector<uint8_t> bytes, bool droppable, uint32_t key) {
    if (droppable) {
        for (auto it = q_.rbegin(); it != q_.rend(); ++it) {
            if (it->droppable && it->key == key && !it->tried && it->sent == 0) {
                bytes_ -= it->bytes.size();
                bytes_ += bytes.size();
                it->bytes = std::move(bytes);
                ++replaced;
                return;
            }
        }
    }
    Item it;
    bytes_ += bytes.size();
    it.bytes = std::move(bytes);
    it.droppable = droppable;
    it.key = key;
    q_.push_back(std::move(it));
}

// ---------------------------------------------------------------------------------------------
// Addresses

std::string Url::hostHeader() const {
    const bool v6 = host.find(':') != std::string::npos;
    std::string h = v6 ? "[" + host + "]" : host;
    if ((tls && port != 443) || (!tls && port != 80)) h += ":" + std::to_string(port);
    return h;
}

bool Url::parse(const std::string& text, Url& out, std::string* error) {
    out = Url();
    auto bad = [&](const char* w) {
        if (error) *error = std::string(w) + ": " + text;
        return false;
    };
    std::string rest;
    if (istartsWith(text, "ws://")) {
        rest = text.substr(5);
    } else if (istartsWith(text, "wss://")) {
        out.tls = true;
        rest = text.substr(6);
    } else {
        return bad("not a ws:// or wss:// address");
    }
    const size_t slash = rest.find('/');
    std::string hostport = slash == std::string::npos ? rest : rest.substr(0, slash);
    out.path = slash == std::string::npos ? "/" : rest.substr(slash);
    const size_t hash = out.path.find('#');
    if (hash != std::string::npos) out.path = out.path.substr(0, hash);
    if (hostport.find('@') != std::string::npos) return bad("user names in the address are not supported");
    out.port = out.tls ? 443 : 80;
    if (!hostport.empty() && hostport[0] == '[') {
        const size_t e = hostport.find(']');
        if (e == std::string::npos) return bad("a bad IPv6 address");
        out.host = hostport.substr(1, e - 1);
        if (e + 1 < hostport.size()) {
            if (hostport[e + 1] != ':') return bad("a bad port");
            const long p = std::atol(hostport.c_str() + e + 2);
            if (p <= 0 || p > 65535) return bad("a bad port");
            out.port = uint16_t(p);
        }
    } else {
        const size_t c = hostport.rfind(':');
        if (c != std::string::npos) {
            const long p = std::atol(hostport.c_str() + c + 1);
            if (p <= 0 || p > 65535) return bad("a bad port");
            out.port = uint16_t(p);
            out.host = hostport.substr(0, c);
        } else {
            out.host = hostport;
        }
    }
    if (out.host.empty()) return bad("no host");
    return true;
}

// ---------------------------------------------------------------------------------------------
// The client

struct Client::Lookup {
    std::mutex m;
    bool done = false;
    std::vector<sock::Addr> addrs;
    std::string error;
};

#ifndef CC_NET_NO_TLS
namespace {
// The OpenSSL 3 functions the client uses, looked up once.
struct Ossl {
    bool ok = false;
    std::string error;
    const SSL_METHOD* (*pTLS_client_method)() = nullptr;
    SSL_CTX* (*pSSL_CTX_new)(const SSL_METHOD*) = nullptr;
    void (*pSSL_CTX_free)(SSL_CTX*) = nullptr;
    long (*pSSL_CTX_ctrl)(SSL_CTX*, int, long, void*) = nullptr;
    void (*pSSL_CTX_set_verify)(SSL_CTX*, int, int (*)(int, X509_STORE_CTX*)) = nullptr;
    int (*pSSL_CTX_set_default_verify_paths)(SSL_CTX*) = nullptr;
    int (*pSSL_CTX_load_verify_locations)(SSL_CTX*, const char*, const char*) = nullptr;
    SSL* (*pSSL_new)(SSL_CTX*) = nullptr;
    void (*pSSL_free)(SSL*) = nullptr;
    int (*pSSL_set_fd)(SSL*, int) = nullptr;
    long (*pSSL_ctrl)(SSL*, int, long, void*) = nullptr;
    X509_VERIFY_PARAM* (*pSSL_get0_param)(SSL*) = nullptr;
    void (*pSSL_set_connect_state)(SSL*) = nullptr;
    int (*pSSL_do_handshake)(SSL*) = nullptr;
    int (*pSSL_get_error)(const SSL*, int) = nullptr;
    long (*pSSL_get_verify_result)(const SSL*) = nullptr;
    int (*pSSL_write_ex)(SSL*, const void*, size_t, size_t*) = nullptr;
    int (*pSSL_read_ex)(SSL*, void*, size_t, size_t*) = nullptr;
    int (*pSSL_shutdown)(SSL*) = nullptr;
    void (*pX509_VERIFY_PARAM_set_hostflags)(X509_VERIFY_PARAM*, unsigned int) = nullptr;
    int (*pX509_VERIFY_PARAM_set1_host)(X509_VERIFY_PARAM*, const char*, size_t) = nullptr;
    int (*pX509_VERIFY_PARAM_set1_ip_asc)(X509_VERIFY_PARAM*, const char*) = nullptr;
    const char* (*pX509_verify_cert_error_string)(long) = nullptr;
    unsigned long (*pERR_get_error)() = nullptr;
    void (*pERR_error_string_n)(unsigned long, char*, size_t) = nullptr;
    void (*pERR_clear_error)() = nullptr;
};

Ossl loadOssl() {
    Ossl o;
#ifdef _WIN32
    HMODULE ssl = LoadLibraryA("libssl-3-x64.dll");
    HMODULE crypto = LoadLibraryA("libcrypto-3-x64.dll");
    if (!ssl || !crypto) {
        o.error = "wss:// needs OpenSSL 3 (libssl-3-x64.dll)";
        return o;
    }
    auto sym = [&](const char* n) -> void* {
        void* p = reinterpret_cast<void*>(GetProcAddress(ssl, n));
        return p ? p : reinterpret_cast<void*>(GetProcAddress(crypto, n));
    };
#else
    void* h = dlopen("libssl.so.3", RTLD_NOW | RTLD_LOCAL);
    if (!h) h = dlopen("libssl.so", RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        o.error = "wss:// needs OpenSSL 3 (libssl.so.3 was not found)";
        return o;
    }
    // libcrypto's functions come through libssl's handle (its dependency).
    auto sym = [&](const char* n) { return dlsym(h, n); };
#endif
    bool all = true;
    auto get = [&](auto& f, const char* n) {
        f = reinterpret_cast<std::remove_reference_t<decltype(f)>>(sym(n));
        if (!f) {
            all = false;
            if (o.error.empty()) o.error = std::string("wss:// needs OpenSSL 3 (no ") + n + ")";
        }
    };
    get(o.pTLS_client_method, "TLS_client_method");
    get(o.pSSL_CTX_new, "SSL_CTX_new");
    get(o.pSSL_CTX_free, "SSL_CTX_free");
    get(o.pSSL_CTX_ctrl, "SSL_CTX_ctrl");
    get(o.pSSL_CTX_set_verify, "SSL_CTX_set_verify");
    get(o.pSSL_CTX_set_default_verify_paths, "SSL_CTX_set_default_verify_paths");
    get(o.pSSL_CTX_load_verify_locations, "SSL_CTX_load_verify_locations");
    get(o.pSSL_new, "SSL_new");
    get(o.pSSL_free, "SSL_free");
    get(o.pSSL_set_fd, "SSL_set_fd");
    get(o.pSSL_ctrl, "SSL_ctrl");
    get(o.pSSL_get0_param, "SSL_get0_param");
    get(o.pSSL_set_connect_state, "SSL_set_connect_state");
    get(o.pSSL_do_handshake, "SSL_do_handshake");
    get(o.pSSL_get_error, "SSL_get_error");
    get(o.pSSL_get_verify_result, "SSL_get_verify_result");
    get(o.pSSL_write_ex, "SSL_write_ex");
    get(o.pSSL_read_ex, "SSL_read_ex");
    get(o.pSSL_shutdown, "SSL_shutdown");
    get(o.pX509_VERIFY_PARAM_set_hostflags, "X509_VERIFY_PARAM_set_hostflags");
    get(o.pX509_VERIFY_PARAM_set1_host, "X509_VERIFY_PARAM_set1_host");
    get(o.pX509_VERIFY_PARAM_set1_ip_asc, "X509_VERIFY_PARAM_set1_ip_asc");
    get(o.pX509_verify_cert_error_string, "X509_verify_cert_error_string");
    get(o.pERR_get_error, "ERR_get_error");
    get(o.pERR_error_string_n, "ERR_error_string_n");
    get(o.pERR_clear_error, "ERR_clear_error");
    o.ok = all;
    return o;
}

const Ossl& ossl() {
    static const Ossl o = loadOssl();
    return o;
}

std::string sslErrors() {
    const Ossl& o = ossl();
    std::string s;
    unsigned long e;
    while ((e = o.pERR_get_error()) != 0) {
        char b[256];
        o.pERR_error_string_n(e, b, sizeof b);
        if (!s.empty()) s += "; ";
        s += b;
    }
    return s;
}
}  // namespace

struct Client::Tls {
    SSL_CTX* ctx = nullptr;
    SSL* ssl = nullptr;
    bool wantWrite = false;
    ~Tls() {
        if (ssl) ossl().pSSL_free(ssl);
        if (ctx) ossl().pSSL_CTX_free(ctx);
    }
};
#else
struct Client::Tls {
    bool wantWrite = false;
};
#endif

Client::Client() { sock::init(); }

Client::~Client() { abort(); }

void Client::fail(const std::string& why) {
    if (error_.empty()) error_ = why;
    if (fd_ != sock::kNoFd) sock::closeFd(fd_);
    fd_ = sock::kNoFd;
    tls_.reset();
    state_ = Closed;
}

bool Client::open(const std::string& url, const ClientOptions& opt) {
    abort();
    error_.clear();
    state_ = Idle;
    opt_ = opt;
    std::string err;
    if (!Url::parse(url, url_, &err)) {
        error_ = err;
        state_ = Closed;
        return false;
    }
#ifdef CC_NET_NO_TLS
    if (url_.tls) {
        error_ = "this build has no TLS (wss://)";
        state_ = Closed;
        return false;
    }
#endif
    httpIn_.clear();
    dec_ = Decoder();
    out_.clear();
    in_.clear();
    closing_ = false;
    startedAt_ = -1;
    addrs_.clear();
    addrAt_ = 0;
    sock::Addr a;
    if (sock::Addr::parseHost(url_.host, url_.port, a)) {
        addrs_.push_back(a);
        state_ = Connecting;
        return true;
    }
    // Name lookup on a helper thread (it can take seconds); the result is collected in service().
    auto lk = std::make_shared<Lookup>();
    lookup_ = lk;
    const std::string host = url_.host;
    const uint16_t port = url_.port;
    std::thread([lk, host, port]() {
        std::vector<sock::Addr> v;
        std::string e;
        sock::resolve(host, port, v, &e);
        std::lock_guard<std::mutex> g(lk->m);
        lk->addrs = std::move(v);
        lk->error = e;
        lk->done = true;
    }).detach();
    state_ = Resolving;
    return true;
}

void Client::startConnect(double now) {
    (void)now;
    while (addrAt_ < addrs_.size()) {
        std::string err;
        fd_ = sock::tcpConnect(addrs_[addrAt_], &err);
        if (fd_ != sock::kNoFd) {
            peerAddr_ = addrs_[addrAt_];
            return;
        }
        ++addrAt_;
        if (addrAt_ >= addrs_.size()) {
            fail("cannot connect to " + url_.hostHeader() + ": " + err);
            return;
        }
    }
    fail("cannot connect to " + url_.hostHeader());
}

bool Client::tlsStart() {
#ifndef CC_NET_NO_TLS
    const Ossl& o = ossl();
    if (!o.ok) {
        fail(o.error);
        return false;
    }
    tls_ = std::make_unique<Tls>();
    tls_->ctx = o.pSSL_CTX_new(o.pTLS_client_method());
    if (!tls_->ctx) {
        fail("TLS: " + sslErrors());
        return false;
    }
    o.pSSL_CTX_ctrl(tls_->ctx, SSL_CTRL_SET_MIN_PROTO_VERSION, TLS1_2_VERSION, nullptr);
    o.pSSL_CTX_ctrl(tls_->ctx, SSL_CTRL_MODE, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER, nullptr);
    if (opt_.verifyTls) {
        o.pSSL_CTX_set_verify(tls_->ctx, SSL_VERIFY_PEER, nullptr);
        const int ok = opt_.caFile.empty() ? o.pSSL_CTX_set_default_verify_paths(tls_->ctx)
                                           : o.pSSL_CTX_load_verify_locations(tls_->ctx, opt_.caFile.c_str(), nullptr);
        if (ok != 1) {
            fail("TLS: cannot load the trusted certificates" + (opt_.caFile.empty() ? std::string() : " from " + opt_.caFile));
            return false;
        }
    } else {
        o.pSSL_CTX_set_verify(tls_->ctx, SSL_VERIFY_NONE, nullptr);
    }
    tls_->ssl = o.pSSL_new(tls_->ctx);
    if (!tls_->ssl || o.pSSL_set_fd(tls_->ssl, int(fd_)) != 1) {
        fail("TLS: " + sslErrors());
        return false;
    }
    sock::Addr lit;
    const bool isIp = sock::Addr::parseHost(url_.host, 0, lit);
    if (!isIp) o.pSSL_ctrl(tls_->ssl, SSL_CTRL_SET_TLSEXT_HOSTNAME, TLSEXT_NAMETYPE_host_name, const_cast<char*>(url_.host.c_str()));
    if (opt_.verifyTls) {
        X509_VERIFY_PARAM* param = o.pSSL_get0_param(tls_->ssl);
        o.pX509_VERIFY_PARAM_set_hostflags(param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
        const int ok = isIp ? o.pX509_VERIFY_PARAM_set1_ip_asc(param, url_.host.c_str()) : o.pX509_VERIFY_PARAM_set1_host(param, url_.host.c_str(), 0);
        if (ok != 1) {
            fail("TLS: cannot check the name " + url_.host);
            return false;
        }
    }
    o.pSSL_set_connect_state(tls_->ssl);
    state_ = TlsHandshake;
    return true;
#else
    fail("this build has no TLS (wss://)");
    return false;
#endif
}

bool Client::tlsStep() {
#ifndef CC_NET_NO_TLS
    const Ossl& o = ossl();
    o.pERR_clear_error();
    const int r = o.pSSL_do_handshake(tls_->ssl);
    if (r == 1) {
        tls_->wantWrite = false;
        return true;
    }
    const int e = o.pSSL_get_error(tls_->ssl, r);
    if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
        tls_->wantWrite = e == SSL_ERROR_WANT_WRITE;
        return false;
    }
    std::string why = sslErrors();
    const long vr = o.pSSL_get_verify_result(tls_->ssl);
    if (vr != X509_V_OK) why = std::string("certificate: ") + o.pX509_verify_cert_error_string(vr);
    fail("TLS handshake with " + url_.hostHeader() + " failed" + (why.empty() ? std::string() : ": " + why));
    return false;
#else
    return false;
#endif
}

long Client::rawWrite(const uint8_t* p, size_t n) {
#ifndef CC_NET_NO_TLS
    if (tls_ && tls_->ssl) {
        const Ossl& o = ossl();
        o.pERR_clear_error();
        size_t w = 0;
        if (o.pSSL_write_ex(tls_->ssl, p, n, &w) == 1) {
            tls_->wantWrite = false;
            return long(w);
        }
        const int e = o.pSSL_get_error(tls_->ssl, 0);
        if (e == SSL_ERROR_WANT_WRITE || e == SSL_ERROR_WANT_READ) {
            tls_->wantWrite = e == SSL_ERROR_WANT_WRITE;
            return 0;
        }
        return -1;
    }
#endif
    const long r = sock::send(fd_, p, n);
    if (r == sock::kWouldBlock) return 0;
    return r < 0 ? -1 : r;
}

// > 0 bytes, 0 would block, −1 closed or failed.
long Client::rawRead(uint8_t* p, size_t n) {
#ifndef CC_NET_NO_TLS
    if (tls_ && tls_->ssl) {
        const Ossl& o = ossl();
        o.pERR_clear_error();
        size_t got = 0;
        if (o.pSSL_read_ex(tls_->ssl, p, n, &got) == 1) return long(got);
        const int e = o.pSSL_get_error(tls_->ssl, 0);
        if (e == SSL_ERROR_WANT_READ) return 0;
        if (e == SSL_ERROR_WANT_WRITE) {
            tls_->wantWrite = true;
            return 0;
        }
        return -1;
    }
#endif
    const long r = sock::recv(fd_, p, n);
    if (r == sock::kWouldBlock) return 0;
    if (r <= 0) return -1;
    return r;
}

bool Client::wantsWrite() const {
    if (state_ == Connecting) return true;
    if (tls_ && tls_->wantWrite) return true;
    return !out_.empty();
}

void Client::readAll() {
    uint8_t buf[65536];
    for (int rounds = 0; rounds < 64 && fd_ != sock::kNoFd; ++rounds) {
        const long n = rawRead(buf, sizeof buf);
        if (n == 0) return;
        if (n < 0) {
            if (state_ == Open && !closing_) fail("the server closed the connection");
            else fail(error_.empty() ? "closed" : error_);
            return;
        }
        if (state_ == Upgrading) {
            httpIn_.append(reinterpret_cast<const char*>(buf), size_t(n));
            const size_t end = httpIn_.find("\r\n\r\n");
            if (end == std::string::npos) {
                if (httpIn_.size() > 16384) fail("a bad answer from the server");
                continue;
            }
            const std::string head = httpIn_.substr(0, end);
            const std::string rest = httpIn_.substr(end + 4);
            httpIn_.clear();
            const std::vector<std::string> lines = split(head, '\n');
            if (lines.empty() || lines[0].find(" 101") == std::string::npos) {
                fail("the server refused the WebSocket (" + trim(lines.empty() ? std::string() : lines[0]) + ")");
                return;
            }
            bool accepted = false, upgrade = false;
            for (size_t i = 1; i < lines.size(); ++i) {
                const size_t c = lines[i].find(':');
                if (c == std::string::npos) continue;
                const std::string k = toLower(trim(lines[i].substr(0, c)));
                const std::string v = trim(lines[i].substr(c + 1));
                if (k == "sec-websocket-accept") accepted = v == acceptKey(key_);
                if (k == "upgrade") upgrade = iequals(v, "websocket");
            }
            if (!accepted || !upgrade) {
                fail("the server's WebSocket answer is wrong");
                return;
            }
            state_ = Open;
            if (rest.empty()) continue;
            std::vector<Decoder::Message> ms;
            if (!dec_.feed(reinterpret_cast<const uint8_t*>(rest.data()), rest.size(), ms)) {
                fail("a bad WebSocket frame from the server");
                return;
            }
            for (auto& m : ms) in_.push_back(std::move(m));
            continue;
        }
        std::vector<Decoder::Message> ms;
        if (!dec_.feed(buf, size_t(n), ms)) {
            fail("a bad WebSocket frame from the server");
            return;
        }
        for (auto& m : ms) {
            if (m.op == Ping) {
                std::vector<uint8_t> f;
                encodeFrame(f, Pong, m.data.data(), m.data.size(), true);
                out_.push(std::move(f));
            } else if (m.op == Close) {
                if (!closing_) {
                    std::vector<uint8_t> f;
                    encodeFrame(f, Close, m.data.data(), m.data.size() >= 2 ? 2 : 0, true);
                    out_.push(std::move(f));
                    out_.flush([&](const uint8_t* p, size_t k) { return rawWrite(p, k); });
                }
                closing_ = true;
                fail("the server closed the connection");
                return;
            } else if (m.op == Text || m.op == Binary) {
                in_.push_back(std::move(m));
            }
        }
    }
}

void Client::service(double now) {
    if (state_ == Idle || state_ == Closed) return;
    if (startedAt_ < 0) startedAt_ = now;
    if (state_ == Resolving) {
        std::shared_ptr<Lookup> lk = lookup_;
        bool done = false;
        {
            std::lock_guard<std::mutex> g(lk->m);
            if (lk->done) {
                done = true;
                addrs_ = lk->addrs;
                if (addrs_.empty()) error_ = lk->error.empty() ? "cannot find " + url_.host : lk->error;
            }
        }
        if (!done) {
            if (now - startedAt_ > opt_.connectTimeout) fail("cannot find " + url_.host + " (timed out)");
            return;
        }
        lookup_.reset();
        if (addrs_.empty()) {
            fail(error_);
            return;
        }
        state_ = Connecting;
    }
    if (state_ == Connecting) {
        if (fd_ == sock::kNoFd) {
            startConnect(now);
            if (state_ == Closed) return;
        }
        std::vector<sock::Wait> w(1);
        w[0].fd = fd_;
        w[0].events = sock::kWrite;
        sock::wait(w, 0);
        if (!(w[0].ready & sock::kWrite)) {
            if (now - startedAt_ > opt_.connectTimeout) fail("cannot connect to " + url_.hostHeader() + " (timed out)");
            return;
        }
        std::string err;
        const int r = sock::connectResult(fd_, &err);
        if (r == 0) return;
        if (r < 0) {
            sock::closeFd(fd_);
            fd_ = sock::kNoFd;
            ++addrAt_;
            if (addrAt_ >= addrs_.size()) {
                fail("cannot connect to " + url_.hostHeader() + ": " + err);
                return;
            }
            return;  // the next address on the next call
        }
        if (url_.tls) {
            if (!tlsStart()) return;
        } else if (!sendUpgrade()) {
            return;
        }
    }
    if (state_ == TlsHandshake) {
        if (!tlsStep()) {
            if (state_ != Closed && now - startedAt_ > opt_.connectTimeout) fail("TLS handshake with " + url_.hostHeader() + " timed out");
            return;
        }
        if (!sendUpgrade()) return;
    }
    if (state_ == Upgrading || state_ == Open) {
        if (state_ == Open && !out_.flush([&](const uint8_t* p, size_t k) { return rawWrite(p, k); })) {
            fail("the connection to the server broke");
            return;
        }
        readAll();
        if (state_ == Open && !out_.flush([&](const uint8_t* p, size_t k) { return rawWrite(p, k); })) {
            fail("the connection to the server broke");
            return;
        }
        if (state_ == Upgrading && now - startedAt_ > opt_.connectTimeout) fail("no WebSocket answer from " + url_.hostHeader());
    }
}

bool Client::sendUpgrade() {
    key_ = newKey();
    std::string req = "GET " + url_.path + " HTTP/1.1\r\nHost: " + url_.hostHeader() +
                      "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key_ +
                      "\r\nSec-WebSocket-Version: 13\r\n";
    if (!opt_.origin.empty()) req += "Origin: " + opt_.origin + "\r\n";
    req += "User-Agent: CrazyCity-Insanity\r\n\r\n";
    // The request goes first, ahead of anything already queued; a fresh socket takes a few hundred
    // bytes at once (TLS too: the handshake is done).
    OutQueue q;
    q.push(std::vector<uint8_t>(req.begin(), req.end()));
    if (!q.flush([&](const uint8_t* p, size_t k) { return rawWrite(p, k); }) || !q.empty()) {
        fail("cannot send the WebSocket request");
        return false;
    }
    state_ = Upgrading;
    return true;
}

void Client::sendText(const std::string& s) {
    if (state_ == Closed || closing_) return;
    std::vector<uint8_t> f;
    encodeFrame(f, Text, reinterpret_cast<const uint8_t*>(s.data()), s.size(), true);
    out_.push(std::move(f));
    if (state_ == Open) out_.flush([&](const uint8_t* p, size_t k) { return rawWrite(p, k); });
}

void Client::sendBinary(const uint8_t* data, size_t size, bool droppable, uint32_t key) {
    if (state_ == Closed || closing_) return;
    std::vector<uint8_t> f;
    encodeFrame(f, Binary, data, size, true);
    out_.push(std::move(f), droppable, key);
    if (state_ == Open) out_.flush([&](const uint8_t* p, size_t k) { return rawWrite(p, k); });
}

bool Client::next(Decoder::Message& m) {
    if (in_.empty()) return false;
    m = std::move(in_.front());
    in_.pop_front();
    return true;
}

void Client::close() {
    if (state_ == Open && !closing_) {
        const uint8_t code[2] = {0x03, 0xE8};  // 1000: normal
        std::vector<uint8_t> f;
        encodeFrame(f, Close, code, 2, true);
        out_.push(std::move(f));
        out_.flush([&](const uint8_t* p, size_t k) { return rawWrite(p, k); });
        closing_ = true;
#ifndef CC_NET_NO_TLS
        if (tls_ && tls_->ssl) ossl().pSSL_shutdown(tls_->ssl);
#endif
    }
    if (fd_ != sock::kNoFd) sock::closeFd(fd_);
    fd_ = sock::kNoFd;
    tls_.reset();
    if (state_ != Idle) state_ = Closed;
    out_.clear();
}

void Client::abort() {
    if (fd_ != sock::kNoFd) sock::closeFd(fd_);
    fd_ = sock::kNoFd;
    tls_.reset();
    lookup_.reset();
    if (state_ != Idle) state_ = Closed;
    out_.clear();
    in_.clear();
}

}  // namespace ws
}  // namespace cc
