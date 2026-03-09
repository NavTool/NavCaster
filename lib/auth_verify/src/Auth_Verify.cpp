#include "Auth_Verify.h"
#include <spdlog/spdlog.h>

#include "auth_verify_internal.h"

int AUTH::Init(AuthVerifyOpt opt, event_base *base)
{

    auth_internal::getInstance()->init(opt, base);
    auth_internal::getInstance()->start();
}

int AUTH::Free()
{
    return auth_internal::getInstance()->stop();
}

int AUTH::Verify(const char *user_name, const char *user_pwd, VerifyCallback cb, void *arg, AuthType type)
{
    return auth_internal::getInstance()->verify(user_name, user_pwd, cb, arg, type);
}

int AUTH::Add_Login_Record(const char *user_name, const char *connect_key, VerifyCallback cb, void *arg, AuthType type)
{
    return auth_internal::getInstance()->add_login_record(user_name, connect_key, cb, arg, type);
}

int AUTH::Add_Logout_Record(const char *user_name, const char *connect_key, AuthType type)
{
    return auth_internal::getInstance()->add_logout_record(user_name, connect_key, type);
}
