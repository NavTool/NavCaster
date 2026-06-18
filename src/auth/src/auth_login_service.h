#pragma once

#include "Auth_Verify.h"
#include "account_schema.h"
#include "auth_record_limit.h"
#include "core_result.h"

#include <ctime>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace navcaster::auth
{

enum class AuthLoginStage
{
    Unknown,
    AnonymousAccepted,
    AccountLookup,
    AccountParsed,
    AccountRejected,
    PasswordRejected,
    RecordLimitEvaluated,
    Accepted
};

struct AuthLoginOptions
{
    bool server_anonymous_login = false;
    bool client_anonymous_login = false;
    bool source_anonymous_login = false;
    bool server_online_protection = false;
    bool client_online_protection = false;
};

struct AuthAccountDecision
{
    navcaster::core::CoreResult result = navcaster::core::CoreResult::success("auth.evaluate_account");
    AuthLoginStage stage = AuthLoginStage::Unknown;
    account_schema::AccountAuthView view;
    nlohmann::json active_record;
    std::string group_uid = account_schema::DEFAULT_GROUP_UID;
    int connect_limit = account_schema::UNLIMITED_CONNECTIONS;
    std::string legacy_reply;
};

struct AuthRecordLimitPlan
{
    navcaster::core::CoreResult result = navcaster::core::CoreResult::success("auth.evaluate_record_limit");
    AuthLoginStage stage = AuthLoginStage::Unknown;
    bool current_allowed = false;
    std::vector<std::string> evicted_connect_keys;
    std::string legacy_reply;
};

class AuthLoginService
{
public:
    static const char *stage_name(AuthLoginStage stage)
    {
        switch (stage)
        {
        case AuthLoginStage::AnonymousAccepted:
            return "anonymous_accepted";
        case AuthLoginStage::AccountLookup:
            return "account_lookup";
        case AuthLoginStage::AccountParsed:
            return "account_parsed";
        case AuthLoginStage::AccountRejected:
            return "account_rejected";
        case AuthLoginStage::PasswordRejected:
            return "password_rejected";
        case AuthLoginStage::RecordLimitEvaluated:
            return "record_limit_evaluated";
        case AuthLoginStage::Accepted:
            return "accepted";
        case AuthLoginStage::Unknown:
        default:
            return "unknown";
        }
    }

    static const char *auth_type_name(AuthType type)
    {
        switch (type)
        {
        case AuthType::SERVER:
            return "server";
        case AuthType::CLIENT:
            return "client";
        case AuthType::SOURCE:
            return "source";
        case AuthType::UNKNOWN:
        default:
            return "unknown";
        }
    }

    static bool anonymous_enabled(AuthType type, const AuthLoginOptions &options)
    {
        switch (type)
        {
        case AuthType::SERVER:
            return options.server_anonymous_login;
        case AuthType::CLIENT:
            return options.client_anonymous_login;
        case AuthType::SOURCE:
            return options.source_anonymous_login;
        case AuthType::UNKNOWN:
        default:
            return false;
        }
    }

    static bool online_protection_enabled(AuthType type, const AuthLoginOptions &options)
    {
        switch (type)
        {
        case AuthType::SERVER:
            return options.server_online_protection;
        case AuthType::CLIENT:
            return options.client_online_protection;
        case AuthType::SOURCE:
        case AuthType::UNKNOWN:
        default:
            return false;
        }
    }

    static AuthAccountDecision account_not_found()
    {
        AuthAccountDecision decision;
        decision.stage = AuthLoginStage::AccountRejected;
        decision.result = navcaster::core::CoreResult::failure(navcaster::core::CoreErrorCode::NotFound,
                                                               "auth.evaluate_account",
                                                               "account_not_found");
        decision.legacy_reply = "User Not active or existed!";
        return decision;
    }

    static AuthAccountDecision evaluate_account(const std::string &active_json,
                                                const std::string &password,
                                                AuthType type,
                                                std::time_t now)
    {
        AuthAccountDecision decision;
        decision.stage = AuthLoginStage::AccountLookup;

        try
        {
            decision.active_record = nlohmann::json::parse(active_json);
        }
        catch (const std::exception &)
        {
            decision.stage = AuthLoginStage::AccountRejected;
            decision.result = navcaster::core::CoreResult::failure(navcaster::core::CoreErrorCode::ParseError,
                                                                   "auth.evaluate_account",
                                                                   "invalid account auth json");
            decision.legacy_reply = "User auth info invalid!";
            return decision;
        }

        std::string auth_error;
        if (!account_schema::parse_auth_view(active_json, decision.view, &auth_error))
        {
            decision.stage = AuthLoginStage::AccountRejected;
            decision.result = navcaster::core::CoreResult::failure(navcaster::core::CoreErrorCode::ParseError,
                                                                   "auth.evaluate_account",
                                                                   "invalid account auth view");
            decision.legacy_reply = "User auth info invalid!";
            return decision;
        }

        if (!account_schema::is_login_enabled(decision.active_record, now, &auth_error))
        {
            decision.stage = AuthLoginStage::AccountRejected;
            decision.result = navcaster::core::CoreResult::failure(navcaster::core::CoreErrorCode::PermissionDenied,
                                                                   "auth.evaluate_account",
                                                                   "account_disabled");
            decision.legacy_reply = auth_error.empty() ? "User Not active or existed!" : auth_error.c_str();
            return decision;
        }

        if (!account_schema::password_matches(decision.view, password))
        {
            decision.stage = AuthLoginStage::PasswordRejected;
            decision.result = navcaster::core::CoreResult::failure(navcaster::core::CoreErrorCode::PermissionDenied,
                                                                   "auth.evaluate_account",
                                                                   "password_mismatch");
            decision.legacy_reply = "User Password Error!";
            return decision;
        }

        decision.stage = AuthLoginStage::Accepted;
        decision.result = navcaster::core::CoreResult::success("auth.evaluate_account");
        decision.group_uid = account_schema::normalize_group_uid(decision.view.group_uid);
        decision.connect_limit = decision.view.connection_limit;
        decision.legacy_reply = "";
        return decision;
    }

    static AuthRecordLimitPlan evaluate_record_limit(const std::multimap<std::time_t, std::string> &records,
                                                     const std::string &current_connect_key,
                                                     int connect_limit,
                                                     bool online_protection,
                                                     const std::string &account)
    {
        AuthRecordLimitPlan plan;
        plan.stage = AuthLoginStage::RecordLimitEvaluated;

        const auto decision = plan_record_limit(records, current_connect_key, connect_limit, online_protection);
        plan.current_allowed = decision.current_allowed;
        plan.evicted_connect_keys = decision.evicted_connect_keys;

        if (!plan.current_allowed)
        {
            plan.legacy_reply = "User Connects Upper Limit , kick out this Connect!";
            plan.result = navcaster::core::CoreResult::failure(navcaster::core::CoreErrorCode::StateConflict,
                                                               "auth.evaluate_record_limit",
                                                               "record_limit_exceeded")
                              .with_subject(account);
            return plan;
        }

        plan.result = navcaster::core::CoreResult::success("auth.evaluate_record_limit").with_subject(account);
        return plan;
    }
};

} // namespace navcaster::auth
