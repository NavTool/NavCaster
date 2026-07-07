#include "infra/socket_util.h"

#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

namespace navcaster::caster {

SocketRuntimeGuard::SocketRuntimeGuard()
{
#if defined(_WIN32)
    WSADATA data;
    _ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#endif
}

SocketRuntimeGuard::~SocketRuntimeGuard()
{
#if defined(_WIN32)
    if (_ok) {
        WSACleanup();
    }
#endif
}

PeerAddress peer_address_from_sockaddr(const sockaddr *address, int socklen)
{
    PeerAddress peer;
    if (!address || socklen <= 0) {
        return peer;
    }

    char buffer[INET6_ADDRSTRLEN] = {0};
    if (address->sa_family == AF_INET && socklen >= static_cast<int>(sizeof(sockaddr_in))) {
        const auto *addr = reinterpret_cast<const sockaddr_in *>(address);
        if (evutil_inet_ntop(AF_INET, &addr->sin_addr, buffer, sizeof(buffer))) {
            peer.host = buffer;
        }
        peer.port = ntohs(addr->sin_port);
    } else if (address->sa_family == AF_INET6 && socklen >= static_cast<int>(sizeof(sockaddr_in6))) {
        const auto *addr = reinterpret_cast<const sockaddr_in6 *>(address);
        if (evutil_inet_ntop(AF_INET6, &addr->sin6_addr, buffer, sizeof(buffer))) {
            peer.host = buffer;
        }
        peer.port = ntohs(addr->sin6_port);
    }
    return peer;
}

void close_socket(evutil_socket_t fd)
{
    if (fd >= 0) {
        evutil_closesocket(fd);
    }
}

} // namespace navcaster::caster
