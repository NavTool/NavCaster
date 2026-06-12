#pragma once

#include "controller_response.h"

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace navcaster::http_api
{

struct SourcetableRequest
{
    std::string host;
    int port = 2101;
    std::string username;
    std::string password;
    std::string ntrip_version = "2.0";
};

struct SourcetableFetchResult
{
    bool ok = false;
    std::string response;
    std::string error;
    std::string detail;
};

using SourcetableFetcher = std::function<SourcetableFetchResult(const SourcetableRequest &, const std::string &)>;

nlohmann::json parse_sourcetable_text(const std::string &source_table_text);
std::string build_sourcetable_request(const SourcetableRequest &request);
SourcetableFetchResult fetch_sourcetable_tcp(const SourcetableRequest &request, const std::string &request_text);

class SourcetableService
{
public:
    explicit SourcetableService(SourcetableFetcher fetcher = fetch_sourcetable_tcp);

    ControllerResponse fetch_remote(const std::string &body_text);
    ControllerResponse local_from_text(const std::string &source_table_text);

private:
    SourcetableFetcher _fetcher;
};

} // namespace navcaster::http_api
