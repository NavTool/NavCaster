#pragma once

#include "Caster_Core.h"
#include "core_result.h"

#include <string>
#include <utility>

namespace navcaster::core
{

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
