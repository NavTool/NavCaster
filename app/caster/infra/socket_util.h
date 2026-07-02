#pragma once

#include <cstdint>
#include <string>

#include <event2/util.h>

namespace navcaster::caster {

struct PeerAddress {
    std::string host;
    std::uint16_t port = 0;
};

class SocketRuntimeGuard {
public:
    SocketRuntimeGuard();
    ~SocketRuntimeGuard();

    SocketRuntimeGuard(const SocketRuntimeGuard &) = delete;
    SocketRuntimeGuard &operator=(const SocketRuntimeGuard &) = delete;

    bool ok() const { return ok_; }

private:
    bool ok_ = true;
};

PeerAddress peer_address_from_sockaddr(const sockaddr *address, int socklen);
void close_socket(evutil_socket_t fd);

} // namespace navcaster::caster
