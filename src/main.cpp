// matching engine IPC server
//
// every message (both directions) is a frame: [u32 payload_len][payload_len bytes of payload]

#include "OrderBook.h"
#include "Types.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

using ByteBuf = std::vector<unsigned char>;

constexpr const char* DEFAULT_SOCKET_PATH = "/tmp/exchange.sock";

// requests are tiny (snapshot = 0 bytes, orderPacket = 24 bytes)so anything larger is treated as a protocol violation
constexpr std::uint32_t MAX_REQUEST_FRAME = 1024;

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

// helps in graceful shutdown
volatile std::sig_atomic_t g_stop = 0;
extern "C" void onSignal(int) noexcept { g_stop = 1; }

void putU32LE(ByteBuf& b, std::uint32_t v) {
    b.push_back(static_cast<unsigned char>(v & 0xFF));
    b.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
    b.push_back(static_cast<unsigned char>((v >> 16) & 0xFF));
    b.push_back(static_cast<unsigned char>((v >> 24) & 0xFF));
}

[[nodiscard]] std::uint32_t getU32LE(const unsigned char* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

template <typename T>
void appendStruct(ByteBuf& b, const T& s) {
    const auto* p = reinterpret_cast<const unsigned char*>(&s);
    b.insert(b.end(), p, p + sizeof(T));
}

// encode an event into a byte buffer
void encodeEvent(ByteBuf& body, const exchange::Event& e) {
    using enum exchange::recieptType;
    body.push_back(static_cast<unsigned char>(e.type));
    switch (e.type) {
        case ORDER_ACK:
        case ORDER_REJECT:
        case CANCEL_ACK:
        case CANCEL_REJECT:
        case TRADE_FILL:
        case SELF_TRADE:
            appendStruct(body, e.payload.trade);
            putU32LE(body, e.quantity);
            break;
        case SNAPSHOT_HEADER:
            appendStruct(body, e.payload.snapshot_header);
            break;
        case SNAPSHOT_DATA:
            appendStruct(body, e.payload.snapshot_data);
            break;
        case SNAPSHOT_END:
            break;
    }
}

// append a single response frame to out buffer
void appendResponseFrame(ByteBuf& out, const std::vector<exchange::Event>& events) {
    ByteBuf body;
    putU32LE(body, static_cast<std::uint32_t>(events.size()));
    for (const exchange::Event& e : events) {
        encodeEvent(body, e);
    }
    putU32LE(out, static_cast<std::uint32_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
}

// decode one request payload, run it against the book, and append the response
[[nodiscard]] bool dispatchRequest(const unsigned char* payload, std::uint32_t len, exchange::OrderBook& book, ByteBuf& out) {
    std::vector<exchange::Event> events;

    if (len == 0) {
        book.snapshot(events);
    } else if (len == sizeof(exchange::orderPacket)) {
        exchange::orderPacket pkt{};
        std::memcpy(&pkt, payload, sizeof(pkt));
        if (pkt.action == exchange::Action::CANCEL) {
            book.cancelOrder(pkt.order_id, events);
        } else {
            book.submitOrder(pkt, events);
        }
    } else {
        return false;  // corrupt request
    }
    appendResponseFrame(out, events);
    return true;
}

enum class IoResult { Ok, Closed, Error, Interrupted };

[[nodiscard]] IoResult readExact(int fd, unsigned char* dst, std::size_t n) {
    std::size_t got = 0;
    while (got < n) {
        const ssize_t r = ::recv(fd, dst + got, n - got, 0);
        if (r > 0) {
            got += static_cast<std::size_t>(r);
        } else if (r == 0) {
            return IoResult::Closed;
        } else if (errno == EINTR) {
            if (g_stop) {
                return IoResult::Interrupted;
            }
            continue;
        } else {
            return IoResult::Error;
        }
    }
    return IoResult::Ok;
}

[[nodiscard]] IoResult writeAll(int fd, const unsigned char* src, std::size_t n) {
    std::size_t sent = 0;
    while (sent < n) {
        const ssize_t w = ::send(fd, src + sent, n - sent, MSG_NOSIGNAL);
        if (w > 0) {
            sent += static_cast<std::size_t>(w);
        } else if (w < 0 && errno == EINTR) {
            if (g_stop) {
                return IoResult::Interrupted;
            }
            continue;
        } else {
            return IoResult::Error;
        }
    }
    return IoResult::Ok;
}

void serveClient(int fd, exchange::OrderBook& book) {
    unsigned char header[4];
    ByteBuf payload;
    ByteBuf response;

    while (g_stop == 0) {
        const IoResult rh = readExact(fd, header, sizeof(header));
        if (rh != IoResult::Ok) {
            if (rh == IoResult::Error) {
                std::fprintf(stderr, "engine: read(header) on fd %d: %s\n", fd, std::strerror(errno));
            }
            return;  // closed/error
        }
        const std::uint32_t flen = getU32LE(header);
        if (flen > MAX_REQUEST_FRAME) {
            std::fprintf(stderr, "engine: oversized frame (%u) on fd %d\n", flen, fd);
            return;
        }

        payload.resize(flen);
        if (flen > 0) {
            const IoResult rp = readExact(fd, payload.data(), flen);
            if (rp != IoResult::Ok) {
                if (rp == IoResult::Error || rp == IoResult::Closed) {
                    std::fprintf(stderr, "engine: truncated frame on fd %d\n", fd);
                }
                return;
            }
        }

        response.clear();
        if (!dispatchRequest(payload.data(), flen, book, response)) {
            std::fprintf(stderr, "engine: malformed request (len %u) on fd %d\n", flen, fd);
            return;
        }

        if (writeAll(fd, response.data(), response.size()) != IoResult::Ok) {
            std::fprintf(stderr, "engine: write(response) on fd %d failed\n", fd);
            return;
        }
    }
}

void installSignalHandlers() {

    struct sigaction ign{};
    ign.sa_handler = SIG_IGN;
    ::sigaction(SIGPIPE, &ign, nullptr);

    // SIGINT/SIGTERM request a graceful shutdown
    struct sigaction stop{};
    stop.sa_handler = onSignal;
    ::sigaction(SIGINT, &stop, nullptr);
    ::sigaction(SIGTERM, &stop, nullptr);
}

// create, bind, and listen on the Unix-domain socket
[[nodiscard]] int createListenSocket(const char* path) {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (std::strlen(path) >= sizeof(addr.sun_path)) {
        std::fprintf(stderr, "engine: socket path too long: %s\n", path);
        return -1;
    }
    std::strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    ::unlink(path);  // remove stale socket from a previous run

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        std::fprintf(stderr, "engine: socket: %s\n", std::strerror(errno));
        return -1;
    }
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::fprintf(stderr, "engine: bind(%s): %s\n", path, std::strerror(errno));
        ::close(fd);
        return -1;
    }
    if (::listen(fd, SOMAXCONN) < 0) {
        std::fprintf(stderr, "engine: listen: %s\n", std::strerror(errno));
        ::close(fd);
        return -1;
    }
    return fd;
}

}  // namespace

int main() {
    const char* socket_path = std::getenv("ENGINE_SOCKET");
    if (socket_path == nullptr || socket_path[0] == '\0') {
        socket_path = DEFAULT_SOCKET_PATH;
    }

    installSignalHandlers();
    const int server_fd = createListenSocket(socket_path);
    if (server_fd < 0) {
        return 1;
    }

    exchange::OrderBook book;
    std::fprintf(stderr, "engine: listening on %s\n", socket_path);

    while (g_stop == 0) {
        const int cfd = ::accept(server_fd, nullptr, nullptr);
        if (cfd < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::fprintf(stderr, "engine: accept: %s\n", std::strerror(errno));
            continue;
        }
        serveClient(cfd, book);
        ::close(cfd);
    }

    std::fprintf(stderr, "engine: shutting down\n");
    ::close(server_fd);
    ::unlink(socket_path);
    return 0;
}