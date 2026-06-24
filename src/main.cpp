#include "OrderBook.h"
#include "Types.h"

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

static constexpr const char* SOCKET_PATH = "/tmp/exchange.sock";
static constexpr int MAX_EVENTS = 64;

// TODO: read framed messages from `fd`, dispatch to OrderBook, write responses
void handleClient(int /*client_fd*/, exchange::OrderBook& /*book*/) {
    // TODO
}

// TODO: encode/decode length-prefixed frames over the Unix domain socket
bool readFrame(int /*fd*/, exchange::orderPacket& /*out*/) {
    // TODO
    return false;
}

bool writeEvents(int /*fd*/, const std::vector<exchange::Event>& /*events*/) {
    // TODO
    return false;
}

}  // namespace

int main() {
    // Remove stale socket file
    ::unlink(SOCKET_PATH);

    int server_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::fprintf(stderr, "socket: %s\n", std::strerror(errno));
        return 1;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (::bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::fprintf(stderr, "bind: %s\n", std::strerror(errno));
        ::close(server_fd);
        return 1;
    }

    if (::listen(server_fd, SOMAXCONN) < 0) {
        std::fprintf(stderr, "listen: %s\n", std::strerror(errno));
        ::close(server_fd);
        return 1;
    }

    int epfd = ::epoll_create1(0);
    if (epfd < 0) {
        std::fprintf(stderr, "epoll_create1: %s\n", std::strerror(errno));
        ::close(server_fd);
        return 1;
    }

    epoll_event ev{};
    ev.events  = EPOLLIN;
    ev.data.fd = server_fd;
    if (::epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev) < 0) {
        std::fprintf(stderr, "epoll_ctl: %s\n", std::strerror(errno));
        ::close(epfd);
        ::close(server_fd);
        return 1;
    }

    exchange::OrderBook book;
    epoll_event events[MAX_EVENTS];

    // TODO: epoll dispatch loop — accept new clients, read frames, route to OrderBook
    for (;;) {
        int n = ::epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            std::fprintf(stderr, "epoll_wait: %s\n", std::strerror(errno));
            break;
        }

        for (int i = 0; i < n; ++i) {
            (void)events[i];
            // TODO: handle EPOLLIN on server_fd (accept) and client fds (read/write)
        }
    }

    ::close(epfd);
    ::close(server_fd);
    ::unlink(SOCKET_PATH);
    return 0;
}
