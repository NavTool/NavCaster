#include "ntrip_msg.h"

#include "base64.h"
#include "knt.h"
#include "version.h"

#include <string_view>

#include <spdlog/fmt/fmt.h>

namespace
{
bool starts_with(std::string_view value, std::string_view prefix)
{
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}
}

std::string build_nrtip_reply(ConnectType type, bool version2, bool chunked)
{
    std::string str;
    if (type == CONNECT_TYPE_SERVER)
    {
        if (version2)
        {
            str += fmt::format("HTTP/1.1 200 OK\r\n");
            str += fmt::format("Ntrip-Version: Ntrip/2.0\r\n");
            str += fmt::format("Server: Ntrip {}_{}/2.0\r\n", PROJECT_SET_NAME, PROJECT_SET_VERSION);
            str += fmt::format("Date: {}\r\n", util_get_http_date());
            if (chunked)
            {
                str += fmt::format("Transfer-Encoding: chunked\r\n");
            }
            str += fmt::format("Connection: close\r\n");
            str += fmt::format("\r\n");
        }
        else
        {
            str += fmt::format("ICY 200 OK\r\n");
            str += fmt::format("\r\n");
        }
    }
    else if (type == CONNECT_TYPE_CLIENT)
    {
        if (version2)
        {
            str += fmt::format("HTTP/1.1 200 OK\r\n");
            str += fmt::format("Ntrip-Version: Ntrip/2.0\r\n");
            str += fmt::format("Server: Ntrip {}_{}/2.0\r\n", PROJECT_SET_NAME, PROJECT_SET_VERSION);
            str += fmt::format("Date: {}\r\n", util_get_http_date());
            str += fmt::format("Cache-Control: no-store, no-cache, max-age=0\r\n");
            str += fmt::format("Pragma: no-cache\r\n");
            str += fmt::format("Connection: close\r\n");
            if (chunked)
            {
                str += fmt::format("Transfer-Encoding: chunked\r\n");
            }
            str += fmt::format("Content-Type: gnss/data\r\n");
            str += fmt::format("\r\n");
        }
        else
        {
            str += fmt::format("ICY 200 OK\r\n");
            str += fmt::format("\r\n");
        }
    }

    return str;
}

std::string build_ntrip_request(ConnectType type, bool version2, std::string mpt, std::string host, std::string auth)
{
    std::string auth_b64 = auth.empty() ? "" : util_base64_encode(auth.c_str());

    std::string password;
    auto colon = auth.find(':');
    if (colon != std::string::npos)
    {
        password = auth.substr(colon + 1);
    }
    else
    {
        password = auth;
    }

    std::string str;
    if (type == CONNECT_TYPE_PULL)
    {
        if (version2)
        {
            str += fmt::format("GET /{} HTTP/1.1\r\n", mpt);
            str += fmt::format("Host: {}\r\n", host);
            str += fmt::format("Ntrip-Version: Ntrip/2.0\r\n");
            str += fmt::format("User-Agent: NTRIP {}/{}\r\n", PROJECT_SET_NAME, PROJECT_SET_VERSION);
            str += fmt::format("Authorization: Basic {}\r\n", auth_b64);
            str += fmt::format("Connection: close\r\n");
            str += fmt::format("\r\n");
        }
        else
        {
            str += fmt::format("GET /{} HTTP/1.0\r\n", mpt);
            str += fmt::format("User-Agent: NTRIP {}/{}\r\n", PROJECT_SET_NAME, PROJECT_SET_VERSION);
            str += fmt::format("Authorization: Basic {}\r\n", auth_b64);
            str += fmt::format("\r\n");
        }
    }
    else if (type == CONNECT_TYPE_PUSH)
    {
        if (version2)
        {
            str += fmt::format("POST /{} HTTP/1.1\r\n", mpt);
            str += fmt::format("Host: {}\r\n", host);
            str += fmt::format("Ntrip-Version: Ntrip/2.0\r\n");
            str += fmt::format("Authorization: Basic {}\r\n", auth_b64);
            str += fmt::format("User-Agent: NTRIP {}/{}\r\n", PROJECT_SET_NAME, PROJECT_SET_VERSION);
            str += fmt::format("Transfer-Encoding: chunked\r\n");
            str += fmt::format("Connection: close\r\n");
            str += fmt::format("\r\n");
        }
        else
        {
            str += fmt::format("SOURCE {} /{} HTTP/1.1\r\n", password, mpt);
            str += fmt::format("Source-Agent: NTRIP {}/{}\r\n", PROJECT_SET_NAME, PROJECT_SET_VERSION);
            str += fmt::format("\r\n");
        }
    }

    return str;
}

bool verify_ntrip_response(const char *data, size_t len, bool &version2, bool &chunked)
{
    if (!data || len == 0)
    {
        return false;
    }

    std::string_view resp(data, len);

    if (starts_with(resp, "ICY 200 OK"))
    {
        version2 = false;
        chunked = false;
        return true;
    }

    if (starts_with(resp, "HTTP/1."))
    {
        auto sp1 = resp.find(' ');
        if (sp1 == std::string_view::npos || sp1 + 3 >= resp.size())
        {
            return false;
        }
        if (resp.substr(sp1 + 1, 3) != "200")
        {
            return false;
        }

        version2 = true;
        chunked = false;

        auto pos = resp.find("chunked");
        if (pos != std::string_view::npos)
        {
            auto line_start = resp.rfind('\n', pos);
            if (line_start != std::string_view::npos)
            {
                auto header_line = resp.substr(line_start + 1, pos - line_start - 1);
                if (header_line.find("ransfer-") != std::string_view::npos)
                {
                    chunked = true;
                }
            }
        }
        return true;
    }

    if (starts_with(resp, "OK"))
    {
        version2 = false;
        chunked = false;
        return true;
    }

    if (starts_with(resp, "SOURCETABLE 200 OK"))
    {
        version2 = false;
        chunked = false;
        return false;
    }

    return false;
}