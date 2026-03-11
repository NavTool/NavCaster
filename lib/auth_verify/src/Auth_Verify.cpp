#include "Auth_Verify.h"
#include <spdlog/spdlog.h>

#include "auth_verify_internal.h"

int AUTH::Init(AuthVerifyOpt opt, event_base *base)
{
    verify_internal::getInstance()->init(opt, base);
    verify_internal::getInstance()->start();
    return 0;
}

int AUTH::Free()
{
    return verify_internal::getInstance()->stop();
}

int AUTH::Verify(const char *user_name, const char *user_pwd, VerifyCallback cb, void *arg, AuthType type)
{
    return verify_internal::getInstance()->verify(user_name, user_pwd, cb, arg, type);
}

int AUTH::Add_Login_Record(const char *user_name, const char *connect_key, VerifyCallback cb, void *arg, AuthType type)
{
    return verify_internal::getInstance()->add_login_record(user_name, connect_key, cb, arg, type);
}

int AUTH::Add_Logout_Record(const char *user_name, const char *connect_key, AuthType type)
{
    return verify_internal::getInstance()->add_logout_record(user_name, connect_key, type);
}
