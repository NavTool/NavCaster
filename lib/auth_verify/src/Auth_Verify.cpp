#include "Auth_Verify.h"
#include <spdlog/spdlog.h>

#include "auth_verify_internal.h"

int AUTH::Init(const char *json_conf, event_base *base)
{
    json conf = json::parse(json_conf);

    auth_internal::getInstance()->init(conf, base);
    auth_internal::getInstance()->start();

    return 0;
}

int AUTH::Free()
{
    auth_internal::getInstance()->stop();
    return 0;
}

int AUTH::Verify(const char *user_name, const char *user_pwd, VerifyCallback cb, void *arg, AuthType type)
{
    return auth_internal::getInstance()->verify(user_name, user_pwd,cb, arg, type);
}

int AUTH::Add_Login_Record(const char *user_name, const char *connect_key, VerifyCallback cb, void *arg, AuthType type)
{
    return auth_internal::getInstance()->add_login_record(user_name, connect_key, cb, arg, type);
}

int AUTH::Add_Logout_Record(const char *user_name, const char *connect_key, AuthType type)
{
    return 0;
}

int AUTH::Add_Account_Item()
{
    return 0;
}

int AUTH::Get_Account_Item()
{
    return 0;
}

int AUTH::Set_Account_Item()
{
    return 0;
}

int AUTH::Del_Account_Item()
{
    return 0;
}
