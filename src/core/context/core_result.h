#pragma once

#include "Caster_Core.h"

#include <sstream>
#include <string>
#include <utility>

namespace navcaster::core
{

enum class CoreErrorCode
{
    Ok,
    InvalidArgument,
    NotFound,
    RedisDisconnected,
    RedisError,
    ParseError,
    PublishFailed,
    StateConflict,
    PermissionDenied,
    InternalError
};

inline const char *core_error_code_name(CoreErrorCode code)
{
    switch (code)
    {
    case CoreErrorCode::Ok:
        return "ok";
    case CoreErrorCode::InvalidArgument:
        return "invalid_argument";
    case CoreErrorCode::NotFound:
        return "not_found";
    case CoreErrorCode::RedisDisconnected:
        return "redis_disconnected";
    case CoreErrorCode::RedisError:
        return "redis_error";
    case CoreErrorCode::ParseError:
        return "parse_error";
    case CoreErrorCode::PublishFailed:
        return "publish_failed";
    case CoreErrorCode::StateConflict:
        return "state_conflict";
    case CoreErrorCode::PermissionDenied:
        return "permission_denied";
    case CoreErrorCode::InternalError:
        return "internal_error";
    }
    return "internal_error";
}

struct CoreResult
{
    CoreErrorCode code = CoreErrorCode::Ok;
    std::string message;
    std::string operation;
    std::string subject;
    std::string redis_key;

    static CoreResult success(std::string operation_name = {})
    {
        CoreResult result;
        result.operation = std::move(operation_name);
        return result;
    }

    static CoreResult failure(CoreErrorCode error_code, std::string operation_name, std::string safe_message)
    {
        CoreResult result;
        result.code = error_code;
        result.operation = std::move(operation_name);
        result.message = std::move(safe_message);
        return result;
    }

    bool ok() const
    {
        return code == CoreErrorCode::Ok;
    }

    CoreResult &with_subject(std::string value)
    {
        subject = std::move(value);
        return *this;
    }

    CoreResult &with_redis_key(std::string value)
    {
        redis_key = std::move(value);
        return *this;
    }

    std::string callback_message() const
    {
        if (!message.empty())
        {
            return message;
        }
        return core_error_code_name(code);
    }

    std::string summary() const
    {
        std::ostringstream out;
        out << core_error_code_name(code);
        if (!operation.empty())
        {
            out << " operation=" << operation;
        }
        if (!subject.empty())
        {
            out << " subject=" << subject;
        }
        if (!redis_key.empty())
        {
            out << " redis_key=" << redis_key;
        }
        if (!message.empty())
        {
            out << " reason=" << message;
        }
        return out.str();
    }
};

inline int to_legacy_int(const CoreResult &result, int failure_value = 1)
{
    return result.ok() ? 0 : failure_value;
}

struct CoreCallbackReply
{
    std::string message;
    caster_reply reply{};

    explicit CoreCallbackReply(const CoreResult &result)
        : message(result.callback_message())
    {
        reply.type = result.ok() ? CasterReply::OK : CasterReply::ERR;
        reply.str = message.c_str();
        reply.len = message.size();
    }

    CoreCallbackReply(const CoreCallbackReply &other)
        : message(other.message), reply(other.reply)
    {
        reply.str = message.c_str();
    }

    CoreCallbackReply(CoreCallbackReply &&other) noexcept
        : message(std::move(other.message)), reply(other.reply)
    {
        reply.str = message.c_str();
    }

    CoreCallbackReply &operator=(const CoreCallbackReply &other)
    {
        if (this != &other)
        {
            message = other.message;
            reply = other.reply;
            reply.str = message.c_str();
        }
        return *this;
    }

    CoreCallbackReply &operator=(CoreCallbackReply &&other) noexcept
    {
        if (this != &other)
        {
            message = std::move(other.message);
            reply = other.reply;
            reply.str = message.c_str();
        }
        return *this;
    }
};

inline void invoke_caster_callback(CasterCallback cb, void *arg, const CoreResult &result)
{
    if (!cb)
    {
        return;
    }
    CoreCallbackReply callback_reply(result);
    cb(nullptr, arg, &callback_reply.reply);
}

} // namespace navcaster::core
