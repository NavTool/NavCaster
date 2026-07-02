#include "sourcetable_service.h"

#include "base64.h"
#include "controller_helpers.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace navcaster::http_api
{
namespace
{
using json = nlohmann::json;

#ifdef _WIN32
using socket_handle_t = SOCKET;
constexpr socket_handle_t invalid_socket_handle = INVALID_SOCKET;

bool ensure_winsock()
{
    static const bool initialized = []() {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return initialized;
}

void close_socket(socket_handle_t socket)
{
    closesocket(socket);
}

std::string socket_error_message()
{
    return std::to_string(WSAGetLastError());
}

std::string gai_error_message(int code)
{
    return gai_strerrorA(code);
}

void set_socket_timeouts(socket_handle_t socket)
{
    DWORD timeout_ms = 5000;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout_ms), sizeof(timeout_ms));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout_ms), sizeof(timeout_ms));
}
#else
using socket_handle_t = int;
constexpr socket_handle_t invalid_socket_handle = -1;

bool ensure_winsock()
{
    return true;
}

void close_socket(socket_handle_t socket)
{
    close(socket);
}

std::string socket_error_message()
{
    return std::strerror(errno);
}

std::string gai_error_message(int code)
{
    return gai_strerror(code);
}

void set_socket_timeouts(socket_handle_t socket)
{
    timeval timeout{5, 0};
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}
#endif

std::vector<std::string> split_semicolon_fields(const std::string &line)
{
    std::vector<std::string> fields;
    std::string field;
    std::istringstream stream(line);
    while (std::getline(stream, field, ';'))
    {
        fields.push_back(field);
    }
    return fields;
}

ControllerResponse sourcetable_response(const json &mountpoints)
{
    return json_response(200, json{{"ok", true}, {"mountpoints", mountpoints}});
}
} // namespace

SourcetableService::SourcetableService(SourcetableFetcher fetcher)
    : _fetcher(std::move(fetcher))
{
}

ControllerResponse SourcetableService::fetch_remote(const std::string &body_text)
{
    json body;
    if (!parse_json_body(body_text, body))
    {
        return error_response(400, "Invalid JSON");
    }

    SourcetableRequest request;
    request.host = body.value("host", std::string{});
    request.port = body.value("port", 2101);
    request.username = body.value("username", std::string{});
    request.password = body.value("password", std::string{});
    request.ntrip_version = body.value("ntrip_version", std::string{"2.0"});

    if (request.host.empty())
    {
        return error_response(400, "Missing host");
    }

    const std::string request_text = build_sourcetable_request(request);
    auto result = _fetcher(request, request_text);
    if (!result.ok)
    {
        if (result.detail.empty())
        {
            return error_response(502, result.error);
        }
        return json_response(502, json{{"error", result.error}, {"detail", result.detail}});
    }

    if (result.response.empty())
    {
        return error_response(502, "No response from server");
    }

    return sourcetable_response(parse_sourcetable_text(result.response));
}

ControllerResponse SourcetableService::local_from_text(const std::string &source_table_text)
{
    return sourcetable_response(parse_sourcetable_text(source_table_text));
}

nlohmann::json parse_sourcetable_text(const std::string &source_table_text)
{
    json mountpoints = json::array();
    std::istringstream stream(source_table_text);
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        if (line.rfind("STR;", 0) == 0)
        {
            const auto fields = split_semicolon_fields(line);
            if (fields.size() >= 2)
            {
                json entry;
                entry["mountpoint"] = fields[1];
                if (fields.size() > 2) entry["identifier"] = fields[2];
                if (fields.size() > 3) entry["format"] = fields[3];
                if (fields.size() > 4) entry["format_details"] = fields[4];
                if (fields.size() > 8) entry["country"] = fields[8];
                if (fields.size() > 9) entry["latitude"] = fields[9];
                if (fields.size() > 10) entry["longitude"] = fields[10];
                mountpoints.push_back(entry);
            }
        }

        if (line.find("ENDSOURCETABLE") != std::string::npos)
        {
            break;
        }
    }
    return mountpoints;
}

std::string build_sourcetable_request(const SourcetableRequest &request)
{
    std::string request_text;
    const std::string host_header = request.host + ":" + std::to_string(request.port);
    if (request.ntrip_version == "1.0")
    {
        request_text = "GET / HTTP/1.0\r\nHost: " + host_header + "\r\n"
                       "User-Agent: NTRIP NavCaster/1.0\r\n";
    }
    else
    {
        request_text = "GET / HTTP/1.1\r\nHost: " + host_header + "\r\n"
                       "Ntrip-Version: Ntrip/2.0\r\n"
                       "User-Agent: NTRIP NavCaster/2.0\r\n"
                       "Connection: close\r\n";
    }

    if (!request.username.empty())
    {
        const std::string credentials = request.username + ":" + request.password;
        request_text += "Authorization: Basic " + util_base64_encode(credentials.c_str()) + "\r\n";
    }
    request_text += "\r\n";
    return request_text;
}

SourcetableFetchResult fetch_sourcetable_tcp(const SourcetableRequest &request, const std::string &request_text)
{
    if (!ensure_winsock())
    {
        return {false, {}, "Socket initialization failed", {}};
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo *resolved = nullptr;
    const int gai = getaddrinfo(request.host.c_str(), std::to_string(request.port).c_str(), &hints, &resolved);
    if (gai != 0 || !resolved)
    {
        if (resolved)
        {
            freeaddrinfo(resolved);
        }
        return {false, {}, "DNS resolve failed", gai_error_message(gai)};
    }

    socket_handle_t socket = ::socket(resolved->ai_family, resolved->ai_socktype, resolved->ai_protocol);
    if (socket == invalid_socket_handle)
    {
        freeaddrinfo(resolved);
        return {false, {}, "Socket creation failed", {}};
    }

    set_socket_timeouts(socket);

    if (::connect(socket, resolved->ai_addr, static_cast<int>(resolved->ai_addrlen)) != 0)
    {
        const std::string detail = socket_error_message();
        freeaddrinfo(resolved);
        close_socket(socket);
        return {false, {}, "Connection failed", detail};
    }
    freeaddrinfo(resolved);

#ifdef _WIN32
    const int sent = ::send(socket, request_text.c_str(), static_cast<int>(request_text.size()), 0);
#else
    const auto sent = ::send(socket, request_text.c_str(), request_text.size(), 0);
#endif
    if (sent <= 0)
    {
        close_socket(socket);
        return {false, {}, "Send failed", {}};
    }

    std::string response;
    char buffer[4096];
    while (true)
    {
#ifdef _WIN32
        const int n = ::recv(socket, buffer, sizeof(buffer), 0);
#else
        const auto n = ::recv(socket, buffer, sizeof(buffer), 0);
#endif
        if (n <= 0)
        {
            break;
        }
        response.append(buffer, static_cast<std::size_t>(n));
        if (response.size() > 65536)
        {
            break;
        }
    }
    close_socket(socket);

    if (response.empty())
    {
        return {false, {}, "No response from server", {}};
    }
    return {true, std::move(response), {}, {}};
}

} // namespace navcaster::http_api
