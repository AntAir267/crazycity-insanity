#include "platform/net_socket.h"

#include <cerrno>
#include <cstring>
#include <mutex>

#ifdef _WIN32
#include <iphlpapi.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace cc {
namespace sock {

namespace {
#ifdef _WIN32
int lastErr() { return WSAGetLastError(); }
bool wouldBlock(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
#else
int lastErr() { return errno; }
bool wouldBlock(int e) { return e == EAGAIN || e == EWOULDBLOCK || e == EINPROGRESS; }
#endif

bool setNonBlocking(Fd fd) {
#ifdef _WIN32
    u_long on = 1;
    return ioctlsocket(fd, FIONBIO, &on) == 0;
#else
    const int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) return false;
    if (fcntl(fd, F_SETFL, fl | O_NONBLOCK) != 0) return false;
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    return true;
#endif
}

void noSigPipe(Fd fd) {
#ifdef SO_NOSIGPIPE
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#else
    (void)fd;
#endif
}

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif
}  // namespace

void init() {
    static std::once_flag once;
    std::call_once(once, []() {
#ifdef _WIN32
        WSADATA w;
        WSAStartup(MAKEWORD(2, 2), &w);
#else
        signal(SIGPIPE, SIG_IGN);
#endif
    });
}

std::string lastError() {
#ifdef _WIN32
    return "socket error " + std::to_string(WSAGetLastError());
#else
    return std::strerror(errno);
#endif
}

// ---- addresses

uint16_t Addr::port() const {
    if (ss.ss_family == AF_INET) return ntohs(reinterpret_cast<const sockaddr_in*>(&ss)->sin_port);
    if (ss.ss_family == AF_INET6) return ntohs(reinterpret_cast<const sockaddr_in6*>(&ss)->sin6_port);
    return 0;
}

void Addr::setPort(uint16_t p) {
    if (ss.ss_family == AF_INET) reinterpret_cast<sockaddr_in*>(&ss)->sin_port = htons(p);
    if (ss.ss_family == AF_INET6) reinterpret_cast<sockaddr_in6*>(&ss)->sin6_port = htons(p);
}

std::string Addr::host() const {
    char b[INET6_ADDRSTRLEN] = {0};
    if (ss.ss_family == AF_INET) inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in*>(&ss)->sin_addr, b, sizeof b);
    else if (ss.ss_family == AF_INET6) inet_ntop(AF_INET6, &reinterpret_cast<const sockaddr_in6*>(&ss)->sin6_addr, b, sizeof b);
    return b;
}

std::string Addr::str() const {
    if (ss.ss_family == AF_INET6) return "[" + host() + "]:" + std::to_string(port());
    if (ss.ss_family == AF_INET) return host() + ":" + std::to_string(port());
    return "?";
}

uint32_t Addr::ipv4() const {
    return ss.ss_family == AF_INET ? reinterpret_cast<const sockaddr_in*>(&ss)->sin_addr.s_addr : 0;
}

bool Addr::isLoopback() const {
    if (ss.ss_family == AF_INET) return (ntohl(ipv4()) >> 24) == 127;
    if (ss.ss_family == AF_INET6) {
        const in6_addr& a = reinterpret_cast<const sockaddr_in6*>(&ss)->sin6_addr;
        static const uint8_t lo[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
        if (std::memcmp(&a, lo, 16) == 0) return true;
        // ::ffff:127.x.x.x
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&a);
        static const uint8_t mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
        return std::memcmp(b, mapped, 12) == 0 && b[12] == 127;
    }
    return false;
}

bool Addr::isPrivate() const {
    uint32_t h = 0;
    if (ss.ss_family == AF_INET) {
        h = ntohl(ipv4());
    } else if (ss.ss_family == AF_INET6) {
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&reinterpret_cast<const sockaddr_in6*>(&ss)->sin6_addr);
        static const uint8_t mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
        if (std::memcmp(b, mapped, 12) == 0) {
            h = (uint32_t(b[12]) << 24) | (uint32_t(b[13]) << 16) | (uint32_t(b[14]) << 8) | b[15];
        } else {
            if (isLoopback()) return true;
            if ((b[0] & 0xFE) == 0xFC) return true;                // fc00::/7
            if (b[0] == 0xFE && (b[1] & 0xC0) == 0x80) return true;  // fe80::/10
            return false;
        }
    } else {
        return false;
    }
    return (h >> 24) == 10 || (h >> 24) == 127 || (h >> 20) == ((172u << 4) | 1u) || (h >> 16) == ((192u << 8) | 168u) ||
           (h >> 16) == ((169u << 8) | 254u);
}

bool Addr::operator==(const Addr& o) const {
    if (ss.ss_family != o.ss.ss_family) return false;
    if (ss.ss_family == AF_INET) {
        const auto* a = reinterpret_cast<const sockaddr_in*>(&ss);
        const auto* b = reinterpret_cast<const sockaddr_in*>(&o.ss);
        return a->sin_port == b->sin_port && a->sin_addr.s_addr == b->sin_addr.s_addr;
    }
    if (ss.ss_family == AF_INET6) {
        const auto* a = reinterpret_cast<const sockaddr_in6*>(&ss);
        const auto* b = reinterpret_cast<const sockaddr_in6*>(&o.ss);
        return a->sin6_port == b->sin6_port && std::memcmp(&a->sin6_addr, &b->sin6_addr, 16) == 0;
    }
    return len == o.len;
}

Addr Addr::ipv4(uint32_t hostOrder, uint16_t port) {
    Addr a;
    auto* s = reinterpret_cast<sockaddr_in*>(&a.ss);
    s->sin_family = AF_INET;
    s->sin_port = htons(port);
    s->sin_addr.s_addr = htonl(hostOrder);
    a.len = sizeof(sockaddr_in);
    return a;
}

Addr Addr::any(uint16_t port, bool v6) {
    Addr a;
    if (v6) {
        auto* s = reinterpret_cast<sockaddr_in6*>(&a.ss);
        s->sin6_family = AF_INET6;
        s->sin6_port = htons(port);
        s->sin6_addr = in6addr_any;
        a.len = sizeof(sockaddr_in6);
    } else {
        a = ipv4(INADDR_ANY, port);
    }
    return a;
}

bool Addr::parseHost(const std::string& host, uint16_t port, Addr& out) {
    out = Addr();
    std::string h = host;
    if (h.size() >= 2 && h.front() == '[' && h.back() == ']') h = h.substr(1, h.size() - 2);
    sockaddr_in s4{};
    if (inet_pton(AF_INET, h.c_str(), &s4.sin_addr) == 1) {
        s4.sin_family = AF_INET;
        s4.sin_port = htons(port);
        std::memcpy(&out.ss, &s4, sizeof s4);
        out.len = sizeof s4;
        return true;
    }
    sockaddr_in6 s6{};
    if (inet_pton(AF_INET6, h.c_str(), &s6.sin6_addr) == 1) {
        s6.sin6_family = AF_INET6;
        s6.sin6_port = htons(port);
        std::memcpy(&out.ss, &s6, sizeof s6);
        out.len = sizeof s6;
        return true;
    }
    return false;
}

bool Addr::parse(const std::string& text, Addr& out) {
    out = Addr();
    std::string host, port;
    if (!text.empty() && text[0] == '[') {
        const size_t e = text.find(']');
        if (e == std::string::npos || e + 1 >= text.size() || text[e + 1] != ':') return false;
        host = text.substr(1, e - 1);
        port = text.substr(e + 2);
    } else {
        const size_t c = text.rfind(':');
        if (c == std::string::npos || text.find(':') != c) return false;
        host = text.substr(0, c);
        port = text.substr(c + 1);
    }
    if (port.empty() || port.size() > 5) return false;
    for (char ch : port)
        if (ch < '0' || ch > '9') return false;
    const long p = std::atol(port.c_str());
    if (p <= 0 || p > 65535) return false;
    return parseHost(host, uint16_t(p), out);
}

bool resolve(const std::string& host, uint16_t port, std::vector<Addr>& out, std::string* error) {
    out.clear();
    Addr a;
    if (Addr::parseHost(host, port, a)) {
        out.push_back(a);
        return true;
    }
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const int rc = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res);
    if (rc != 0 || !res) {
        if (error) *error = "cannot find " + host + " (" + gai_strerror(rc) + ")";
        return false;
    }
    std::vector<Addr> v6;
    for (addrinfo* r = res; r; r = r->ai_next) {
        if (r->ai_addrlen > sizeof(sockaddr_storage)) continue;
        Addr x;
        std::memcpy(&x.ss, r->ai_addr, r->ai_addrlen);
        x.len = socklen_t(r->ai_addrlen);
        bool dup = false;
        for (const Addr& y : out) dup = dup || y == x;
        for (const Addr& y : v6) dup = dup || y == x;
        if (dup) continue;
        (x.v4() ? out : v6).push_back(x);
    }
    freeaddrinfo(res);
    out.insert(out.end(), v6.begin(), v6.end());
    if (out.empty() && error) *error = "no address for " + host;
    return !out.empty();
}

// ---- sockets

Fd tcpConnect(const Addr& to, std::string* error) {
    init();
    Fd fd = ::socket(to.ss.ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (fd == kNoFd) {
        if (error) *error = lastError();
        return kNoFd;
    }
    setNonBlocking(fd);
    noSigPipe(fd);
    setNoDelay(fd);
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&to.ss), to.len) != 0 && !wouldBlock(lastErr())) {
        if (error) *error = lastError();
        closeFd(fd);
        return kNoFd;
    }
    return fd;
}

int connectResult(Fd fd, std::string* error) {
    int err = 0;
    socklen_t n = sizeof err;
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &n) != 0) err = lastErr();
    if (err == 0) {
        // Connected only when the peer address is there.
        sockaddr_storage ss;
        socklen_t sl = sizeof ss;
        if (getpeername(fd, reinterpret_cast<sockaddr*>(&ss), &sl) == 0) return 1;
        return 0;
    }
    if (wouldBlock(err)) return 0;
    if (error) {
#ifdef _WIN32
        *error = "socket error " + std::to_string(err);
#else
        *error = std::strerror(err);
#endif
    }
    return -1;
}

Fd tcpListen(const Addr& at, int backlog, std::string* error) {
    init();
    Fd fd = ::socket(at.ss.ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (fd == kNoFd) {
        if (error) *error = lastError();
        return kNoFd;
    }
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof on);
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&at.ss), at.len) != 0 || ::listen(fd, backlog) != 0) {
        if (error) *error = "cannot listen on " + at.str() + ": " + lastError();
        closeFd(fd);
        return kNoFd;
    }
    setNonBlocking(fd);
    return fd;
}

Fd tcpAccept(Fd listener, Addr* from) {
    Addr a;
    a.len = sizeof a.ss;
    Fd fd = ::accept(listener, reinterpret_cast<sockaddr*>(&a.ss), &a.len);
    if (fd == kNoFd) return kNoFd;
    setNonBlocking(fd);
    noSigPipe(fd);
    setNoDelay(fd);
    if (from) *from = a;
    return fd;
}

Fd udpOpen(const Addr& at, bool broadcast, std::string* error) {
    init();
    Fd fd = ::socket(at.ss.ss_family, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == kNoFd) {
        if (error) *error = lastError();
        return kNoFd;
    }
    if (broadcast) {
        int on = 1;
        setsockopt(fd, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on), sizeof on);
    }
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&at.ss), at.len) != 0) {
        if (error) *error = "cannot bind UDP " + at.str() + ": " + lastError();
        closeFd(fd);
        return kNoFd;
    }
    setNonBlocking(fd);
    return fd;
}

bool localAddr(Fd fd, Addr& out) {
    out = Addr();
    out.len = sizeof out.ss;
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&out.ss), &out.len) != 0) {
        out.len = 0;
        return false;
    }
    return true;
}

void closeFd(Fd fd) {
    if (fd == kNoFd) return;
#ifdef _WIN32
    closesocket(fd);
#else
    ::close(fd);
#endif
}

void setNoDelay(Fd fd) {
    int on = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof on);
}

long send(Fd fd, const void* data, size_t size) {
    for (;;) {
        const auto n = ::send(fd, static_cast<const char*>(data), int(size), kSendFlags);
        if (n >= 0) return long(n);
        const int e = lastErr();
#ifndef _WIN32
        if (e == EINTR) continue;
#endif
        return wouldBlock(e) ? kWouldBlock : kError;
    }
}

long recv(Fd fd, void* data, size_t size) {
    for (;;) {
        const auto n = ::recv(fd, static_cast<char*>(data), int(size), 0);
        if (n > 0) return long(n);
        if (n == 0) return kClosed;
        const int e = lastErr();
#ifndef _WIN32
        if (e == EINTR) continue;
#endif
        return wouldBlock(e) ? kWouldBlock : kError;
    }
}

long sendTo(Fd fd, const void* data, size_t size, const Addr& to) {
    const auto n = ::sendto(fd, static_cast<const char*>(data), int(size), kSendFlags, reinterpret_cast<const sockaddr*>(&to.ss), to.len);
    if (n >= 0) return long(n);
    return wouldBlock(lastErr()) ? kWouldBlock : kError;
}

long recvFrom(Fd fd, void* data, size_t size, Addr& from) {
    for (;;) {
        from.len = sizeof from.ss;
        const auto n = ::recvfrom(fd, static_cast<char*>(data), int(size), 0, reinterpret_cast<sockaddr*>(&from.ss), &from.len);
        if (n >= 0) return long(n);
        const int e = lastErr();
#ifndef _WIN32
        if (e == EINTR) continue;
#else
        // A port-unreachable answer to an earlier datagram: not an error for this socket.
        if (e == WSAECONNRESET) continue;
#endif
        // Linux reports an ICMP port unreachable on the next read: keep reading.
        if (e == ECONNREFUSED) continue;
        return wouldBlock(e) ? kWouldBlock : kError;
    }
}

int wait(std::vector<Wait>& fds, int timeoutMs) {
    if (fds.empty()) {
        if (timeoutMs > 0) {
#ifdef _WIN32
            Sleep(DWORD(timeoutMs));
#else
            ::poll(nullptr, 0, timeoutMs);
#endif
        }
        return 0;
    }
    std::vector<pollfd> p(fds.size());
    for (size_t i = 0; i < fds.size(); ++i) {
        p[i].fd = fds[i].fd;
        p[i].events = short(((fds[i].events & kRead) ? POLLIN : 0) | ((fds[i].events & kWrite) ? POLLOUT : 0));
        p[i].revents = 0;
    }
#ifdef _WIN32
    const int n = WSAPoll(p.data(), ULONG(p.size()), timeoutMs);
#else
    int n;
    do {
        n = ::poll(p.data(), nfds_t(p.size()), timeoutMs);
    } while (n < 0 && errno == EINTR);
#endif
    for (size_t i = 0; i < fds.size(); ++i) {
        int r = 0;
        if (p[i].revents & (POLLIN | POLLHUP | POLLERR)) r |= kRead;
        if (p[i].revents & (POLLOUT | POLLERR | POLLHUP)) r |= kWrite;
        fds[i].ready = r & (fds[i].events | kRead);
    }
    return n;
}

std::vector<Addr> localIPv4(bool withLoopback) {
    std::vector<Addr> out;
#ifdef _WIN32
    ULONG size = 15000;
    std::vector<uint8_t> buf(size);
    auto* aa = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, aa, &size) ==
        NO_ERROR) {
        for (auto* a = aa; a; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp) continue;
            for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
                Addr x;
                std::memcpy(&x.ss, u->Address.lpSockaddr, sizeof(sockaddr_in));
                x.len = sizeof(sockaddr_in);
                if (!withLoopback && x.isLoopback()) continue;
                out.push_back(x);
            }
        }
    }
#else
    ifaddrs* ifs = nullptr;
    if (getifaddrs(&ifs) != 0) return out;
    for (ifaddrs* i = ifs; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
        if (!(i->ifa_flags & IFF_UP)) continue;
        Addr x;
        std::memcpy(&x.ss, i->ifa_addr, sizeof(sockaddr_in));
        x.len = sizeof(sockaddr_in);
        if (!withLoopback && x.isLoopback()) continue;
        bool dup = false;
        for (const Addr& y : out) dup = dup || y.ipv4() == x.ipv4();
        if (!dup) out.push_back(x);
    }
    freeifaddrs(ifs);
#endif
    return out;
}

std::vector<Addr> broadcastIPv4(uint16_t port) {
    std::vector<Addr> out;
    out.push_back(Addr::ipv4(0xFFFFFFFFu, port));
#ifndef _WIN32
    ifaddrs* ifs = nullptr;
    if (getifaddrs(&ifs) == 0) {
        for (ifaddrs* i = ifs; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP)) continue;
            if (!(i->ifa_flags & IFF_BROADCAST) || !i->ifa_broadaddr) continue;
            Addr x;
            std::memcpy(&x.ss, i->ifa_broadaddr, sizeof(sockaddr_in));
            x.len = sizeof(sockaddr_in);
            x.setPort(port);
            bool dup = false;
            for (const Addr& y : out) dup = dup || y == x;
            if (!dup) out.push_back(x);
        }
        freeifaddrs(ifs);
    }
#endif
    return out;
}

}  // namespace sock
}  // namespace cc
