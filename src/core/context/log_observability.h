#pragma once

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace navcaster::observability
{

inline std::string to_lower_ascii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

inline std::string trim_ascii(std::string value)
{
    auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
    while (!value.empty() && is_space(static_cast<unsigned char>(value.front())))
    {
        value.erase(value.begin());
    }
    while (!value.empty() && is_space(static_cast<unsigned char>(value.back())))
    {
        value.pop_back();
    }
    return value;
}

inline bool is_sensitive_key(std::string key)
{
    key = to_lower_ascii(trim_ascii(std::move(key)));
    return key.find("authorization") != std::string::npos ||
           key.find("password") != std::string::npos ||
           key.find("token") != std::string::npos ||
           key.find("secret") != std::string::npos ||
           key.find("salt") != std::string::npos ||
           key.find("hash") != std::string::npos ||
           key == "cookie" ||
           key == "set-cookie";
}

inline std::string redact_header_line(const std::string &line)
{
    const auto pos = line.find(':');
    if (pos == std::string::npos)
    {
        return line;
    }

    auto key = trim_ascii(line.substr(0, pos));
    if (is_sensitive_key(key))
    {
        return key + ": ***";
    }
    return line;
}

inline std::string summarize_ntrip_request_line(const std::string &line)
{
    std::istringstream in(line);
    std::vector<std::string> tokens;
    std::string token;
    while (in >> token)
    {
        tokens.push_back(token);
    }
    if (tokens.empty())
    {
        return "method=<empty>";
    }

    const auto method = tokens[0];
    if (method == "SOURCE")
    {
        std::string mount = "<missing>";
        bool credential_present = false;
        std::string version;
        if (tokens.size() >= 4)
        {
            credential_present = true;
            mount = tokens[2];
            version = tokens[3];
        }
        else if (tokens.size() == 3)
        {
            if (tokens[2].rfind("HTTP/", 0) == 0)
            {
                mount = tokens[1];
                version = tokens[2];
            }
            else
            {
                credential_present = true;
                mount = tokens[2];
            }
        }
        else if (tokens.size() == 2)
        {
            mount = tokens[1];
        }

        std::ostringstream out;
        out << "method=SOURCE mountpoint=" << mount
            << " credential=" << (credential_present ? "present" : "absent");
        if (!version.empty())
        {
            out << " version=" << version;
        }
        return out.str();
    }

    if (method == "GET" || method == "POST")
    {
        std::ostringstream out;
        out << "method=" << method;
        if (tokens.size() > 1)
        {
            out << " target=" << tokens[1];
        }
        if (tokens.size() > 2)
        {
            out << " version=" << tokens[2];
        }
        return out.str();
    }

    return "method=" + method + " token_count=" + std::to_string(tokens.size());
}

} // namespace navcaster::observability
