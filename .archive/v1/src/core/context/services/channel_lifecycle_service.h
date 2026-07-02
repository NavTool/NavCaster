#pragma once

#include "core_result.h"

#include <string>
#include <utility>

namespace navcaster::core
{

enum class ChannelEndpoint
{
    Base,
    Rover
};

struct ChannelBucketPlan
{
    CoreResult result = CoreResult::success();
    bool created = false;
    std::string bucket;
};

class ChannelLifecycleService
{
public:
    static const char *endpoint_name(ChannelEndpoint endpoint)
    {
        switch (endpoint)
        {
        case ChannelEndpoint::Base:
            return "base";
        case ChannelEndpoint::Rover:
            return "rover";
        }
        return "unknown";
    }

    static const char *safe_text(const char *value)
    {
        return value ? value : "";
    }

    static bool missing_text(const char *value)
    {
        return value == nullptr || value[0] == '\0';
    }

    static CoreResult require_channel_identity(const char *operation,
                                               const char *channel,
                                               const char *connect_key)
    {
        if (missing_text(channel))
        {
            return CoreResult::failure(CoreErrorCode::InvalidArgument,
                                       safe_text(operation),
                                       "missing required argument")
                .with_subject("channel");
        }
        if (missing_text(connect_key))
        {
            return CoreResult::failure(CoreErrorCode::InvalidArgument,
                                       safe_text(operation),
                                       "missing required argument")
                .with_subject("connect_key");
        }
        return CoreResult::success(safe_text(operation));
    }

    static std::string registration_bucket(ChannelEndpoint endpoint,
                                           const char *channel,
                                           const char *user_name)
    {
        return endpoint == ChannelEndpoint::Base ? safe_text(channel) : safe_text(user_name);
    }

    static std::string subscription_bucket(const char *channel)
    {
        return safe_text(channel);
    }

    static std::string connection_redis_key(ChannelEndpoint endpoint, const std::string &bucket)
    {
        return endpoint == ChannelEndpoint::Base ? std::string("MPT:REC:") + bucket
                                                 : std::string("USR:REC:") + bucket;
    }

    static std::string subscription_redis_key(ChannelEndpoint endpoint, const std::string &bucket)
    {
        return endpoint == ChannelEndpoint::Base ? std::string("MPT:SUB:") + bucket
                                                 : std::string("USR:SUB:") + bucket;
    }

    static std::string publish_redis_key(ChannelEndpoint endpoint, const std::string &bucket)
    {
        return endpoint == ChannelEndpoint::Base ? std::string("MPT:") + bucket
                                                 : std::string("USR:") + bucket;
    }

    template <typename NestedMap>
    static ChannelBucketPlan ensure_bucket(NestedMap &items,
                                           const char *operation,
                                           std::string bucket,
                                           std::string redis_key = {})
    {
        auto [_, inserted] = items.try_emplace(bucket);
        ChannelBucketPlan plan;
        plan.result = CoreResult::success(safe_text(operation)).with_redis_key(std::move(redis_key));
        plan.created = inserted;
        plan.bucket = std::move(bucket);
        return plan;
    }

    template <typename NestedMap>
    static CoreResult require_bucket(const NestedMap &items,
                                     const char *operation,
                                     const std::string &bucket,
                                     std::string redis_key = {})
    {
        if (items.find(bucket) == items.end())
        {
            return CoreResult::failure(CoreErrorCode::NotFound,
                                       safe_text(operation),
                                       "channel lifecycle bucket not found")
                .with_subject(bucket)
                .with_redis_key(std::move(redis_key));
        }
        return CoreResult::success(safe_text(operation)).with_redis_key(std::move(redis_key));
    }

    template <typename NestedMap>
    static CoreResult require_connect_absent(const NestedMap &items,
                                             const char *operation,
                                             const std::string &bucket,
                                             const char *connect_key,
                                             std::string redis_key = {})
    {
        auto bucket_item = items.find(bucket);
        if (bucket_item != items.end() && bucket_item->second.find(safe_text(connect_key)) != bucket_item->second.end())
        {
            return CoreResult::failure(CoreErrorCode::StateConflict,
                                       safe_text(operation),
                                       "connect_key already exists")
                .with_subject(safe_text(connect_key))
                .with_redis_key(std::move(redis_key));
        }
        return CoreResult::success(safe_text(operation)).with_redis_key(std::move(redis_key));
    }

    template <typename NestedMap>
    static CoreResult require_connect_present(const NestedMap &items,
                                              const char *operation,
                                              const std::string &bucket,
                                              const char *connect_key,
                                              std::string redis_key = {})
    {
        auto bucket_item = items.find(bucket);
        if (bucket_item == items.end())
        {
            return CoreResult::failure(CoreErrorCode::NotFound,
                                       safe_text(operation),
                                       "channel lifecycle bucket not found")
                .with_subject(bucket)
                .with_redis_key(std::move(redis_key));
        }
        if (bucket_item->second.find(safe_text(connect_key)) == bucket_item->second.end())
        {
            return CoreResult::failure(CoreErrorCode::NotFound,
                                       safe_text(operation),
                                       "connect_key not found")
                .with_subject(safe_text(connect_key))
                .with_redis_key(std::move(redis_key));
        }
        return CoreResult::success(safe_text(operation)).with_redis_key(std::move(redis_key));
    }
};

} // namespace navcaster::core
