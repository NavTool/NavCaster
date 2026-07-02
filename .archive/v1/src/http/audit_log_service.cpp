#include "audit_log_service.h"

#include "controller_helpers.h"

#include <algorithm>
#include <vector>

namespace navcaster::http_api
{
namespace
{
using json = nlohmann::json;
constexpr int AUDIT_KEEP = 50000;

long long normalize_limit(long long limit)
{
    if (limit <= 0 || limit > 1000)
    {
        return 100;
    }
    return limit;
}

json parse_payload(const std::string &body)
{
    if (body.empty())
    {
        return nullptr;
    }
    try
    {
        auto payload = json::parse(body);
        mask_audit_secrets(payload);
        return payload;
    }
    catch (...)
    {
        return body;
    }
}
} // namespace

void mask_audit_secrets(json &value)
{
    if (!value.is_object())
    {
        return;
    }
    for (auto &[key, field] : value.items())
    {
        std::string lower_key = key;
        std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);
        if (lower_key == "password" || lower_key == "token" || lower_key == "secret" || lower_key == "admin_password")
        {
            if (field.is_string())
            {
                field = "***";
            }
        }
        else if (field.is_object())
        {
            mask_audit_secrets(field);
        }
    }
}

AuditTarget infer_audit_target(const std::string &path)
{
    AuditTarget target;
    if (path.size() < 6 || path.compare(0, 5, "/api/") != 0)
    {
        return target;
    }

    std::vector<std::string> segments;
    size_t pos = 5;
    while (pos < path.size())
    {
        size_t slash = path.find('/', pos);
        std::string segment = path.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
        if (!segment.empty())
        {
            segments.push_back(segment);
        }
        if (slash == std::string::npos)
        {
            break;
        }
        pos = slash + 1;
    }
    if (segments.empty())
    {
        return target;
    }
    target.type = segments.front();
    if (segments.size() >= 2)
    {
        target.id = segments.back();
    }
    return target;
}

std::string audit_method_string(evhttp_cmd_type method)
{
    switch (method)
    {
    case EVHTTP_REQ_GET:
        return "GET";
    case EVHTTP_REQ_POST:
        return "POST";
    case EVHTTP_REQ_PUT:
        return "PUT";
    case EVHTTP_REQ_DELETE:
        return "DELETE";
    case EVHTTP_REQ_PATCH:
        return "PATCH";
    default:
        return "?";
    }
}

bool should_write_audit(evhttp_cmd_type method, const std::string &path)
{
    if (method != EVHTTP_REQ_POST &&
        method != EVHTTP_REQ_PUT &&
        method != EVHTTP_REQ_DELETE &&
        method != EVHTTP_REQ_PATCH)
    {
        return false;
    }
    return path != "/api/auth/login";
}

AuditLogService::AuditLogService(storage::RedisHashClient &redis)
    : _redis(redis)
{
}

void AuditLogService::write(const HttpRequest &req,
                            const HttpResponse &resp,
                            const std::string &actor,
                            const std::string &client_ip,
                            const std::string &node_id,
                            long long timestamp)
{
    if (!should_write_audit(req.method, req.path))
    {
        return;
    }

    storage::AuditLogRepository repo(_redis);
    const long long id = repo.next_id();
    const AuditTarget target = infer_audit_target(req.path);
    const json payload = parse_payload(req.body);

    json entry = {
        {"id", id},
        {"timestamp", timestamp},
        {"actor", actor.empty() ? std::string{"anonymous"} : actor},
        {"source_ip", client_ip},
        {"node_id", node_id},
        {"action", audit_method_string(req.method) + " " + req.path},
        {"target_type", target.type},
        {"target_id", target.id},
        {"payload", payload.is_null() ? "" : payload.dump()},
        {"result", resp.status_code}
    };

    if (resp.status_code >= 400)
    {
        try
        {
            auto error_body = json::parse(resp.body);
            if (error_body.is_object() && error_body.contains("error"))
            {
                entry["error_message"] = error_body["error"].get<std::string>();
            }
        }
        catch (...)
        {
        }
    }

    repo.append(entry, AUDIT_KEEP);
}

ControllerResponse AuditLogService::list(long long limit,
                                         long long cursor,
                                         const std::string &filter_actor,
                                         const std::string &filter_action,
                                         const std::string &filter_target)
{
    const long long normalized_limit = normalize_limit(limit);
    storage::AuditLogRepository repo(_redis);
    const long long start = cursor;
    const long long stop = cursor + normalized_limit * 4 - 1;
    json records = repo.read(start, stop);

    json items = json::array();
    long long scanned = 0;
    for (const auto &entry : records)
    {
        scanned++;
        if (!entry.is_object())
        {
            continue;
        }
        if (!filter_actor.empty() && entry.value("actor", std::string{}) != filter_actor)
        {
            continue;
        }
        if (!filter_action.empty() && entry.value("action", std::string{}).find(filter_action) == std::string::npos)
        {
            continue;
        }
        if (!filter_target.empty() && entry.value("target_type", std::string{}) != filter_target)
        {
            continue;
        }
        items.push_back(entry);
        if (static_cast<long long>(items.size()) >= normalized_limit)
        {
            break;
        }
    }

    const long long next_cursor = start + scanned;
    const long long total = repo.total();
    return json_response(200, json{{"items", items},
                                   {"next_cursor", next_cursor},
                                   {"has_more", next_cursor < total},
                                   {"total", total}});
}

} // namespace navcaster::http_api
