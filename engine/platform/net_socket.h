// Non-blocking TCP and UDP sockets for the native network code (platform/websocket.*,
// platform/native_transport.*, platform/host_server.*): addresses, connecting, listening, sending
// and receiving without waiting, waiting on several sockets, and this machine's IPv4 addresses.
// POSIX sockets; the few Windows differences are behind _WIN32.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace cc {
namespace sock {

#ifdef _WIN32
using Fd = SOCKET;
const Fd kNoFd = INVALID_SOCKET;
#else
using Fd = int;
const Fd kNoFd = -1;
#endif

// Once per program: Winsock started; on POSIX, SIGPIPE ignored (a peer that closed must not end
// the program when TLS writes to its socket).
void init();

struct Addr {
    sockaddr_storage ss{};
    socklen_t len = 0;

    bool valid() const { return len != 0; }
    bool v4() const { return ss.ss_family == AF_INET; }
    uint16_t port() const;
    void setPort(uint16_t p);
    // "1.2.3.4:5" or "[::1]:5".
    std::string str() const;
    // Only the address ("1.2.3.4" or "::1").
    std::string host() const;
    // An IPv4 address in network order (0 for IPv6).
    uint32_t ipv4() const;
    bool isLoopback() const;
    // Private, link-local or loopback (RFC 1918, RFC 3927, RFC 4193): a LAN neighbour.
    bool isPrivate() const;
    bool operator==(const Addr& o) const;
    bool operator!=(const Addr& o) const { return !(*this == o); }

    static Addr ipv4(uint32_t hostOrder, uint16_t port);
    static Addr any(uint16_t port, bool v6 = false);
    // A numeric address with a port: "1.2.3.4:5", "[::1]:5" (no name lookup).
    static bool parse(const std::string& text, Addr& out);
    // A numeric address without a port.
    static bool parseHost(const std::string& host, uint16_t port, Addr& out);
};

// Name lookup (blocking; numeric addresses return at once). IPv4 first.
bool resolve(const std::string& host, uint16_t port, std::vector<Addr>& out, std::string* error = nullptr);

// A TCP connection being made (non-blocking). kNoFd on an immediate error.
Fd tcpConnect(const Addr& to, std::string* error = nullptr);
// After the socket became writable: 1 connected, 0 still connecting, -1 failed.
int connectResult(Fd fd, std::string* error = nullptr);
// A listening TCP socket (reuse-address set).
Fd tcpListen(const Addr& at, int backlog = 64, std::string* error = nullptr);
// The next connection waiting on a listening socket, kNoFd if none.
Fd tcpAccept(Fd listener, Addr* from = nullptr);
// A UDP socket bound to `at` (port 0: any free port); broadcast allowed when asked.
Fd udpOpen(const Addr& at, bool broadcast = false, std::string* error = nullptr);
// The address a socket is bound to.
bool localAddr(Fd fd, Addr& out);
void closeFd(Fd fd);
void setNoDelay(Fd fd);

// Results of the calls below: bytes moved (> 0), kWouldBlock, kClosed (the peer closed: reads
// only) or kError.
constexpr long kWouldBlock = -2;
constexpr long kClosed = 0;
constexpr long kError = -1;
long send(Fd fd, const void* data, size_t size);
long recv(Fd fd, void* data, size_t size);
long sendTo(Fd fd, const void* data, size_t size, const Addr& to);
long recvFrom(Fd fd, void* data, size_t size, Addr& from);

// Waiting on sockets (poll): each entry's `events` (kRead | kWrite) in, `ready` out. Returns the
// number ready, 0 on time-out, −1 on error.
constexpr int kRead = 1, kWrite = 2;
struct Wait {
    Fd fd = kNoFd;
    int events = kRead;
    int ready = 0;
};
int wait(std::vector<Wait>& fds, int timeoutMs);

// This machine's IPv4 interface addresses that are up (loopback left out unless asked).
std::vector<Addr> localIPv4(bool withLoopback = false);
// The directed broadcast addresses of those interfaces (255.255.255.255 always included).
std::vector<Addr> broadcastIPv4(uint16_t port);

std::string lastError();

}  // namespace sock
}  // namespace cc
