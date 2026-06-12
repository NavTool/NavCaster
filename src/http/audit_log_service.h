#pragma once

#include "audit_log_repository.h"
#include "controller_response.h"
#include "http_server.h"
#include "redis_hash_client.h"

#include <string>

#include <nlohmann/json.hpp>

namespace navcaster::http_api
{

struct AuditTarget
{
    std::string type;
    std::string id;
};

void mask_audit_secrets(nlohmann::json &value);
AuditTarget infer_audit_target(const std::string &path);
std::string audit_method_string(evhttp_cmd_type method);
bool should_write_audit(evhttp_cmd_type method, const std::string &path);

class AuditLogService
{
public:
    explicit AuditLogService(storage::RedisHashClient &redis);

    void write(const HttpRequest &req,
               const HttpResponse &resp,
               const std::string &actor,
               const std::string &client_ip,
               const std::string &node_id,
               long long timestamp);

    ControllerResponse list(long long limit,
                            long long cursor,
                            const std::string &filter_actor,
                            const std::string &filter_action,
                            const std::string &filter_target);

private:
    storage::RedisHashClient &_redis;
};

} // namespace navcaster::http_api
