#pragma once

#include <string>
#include <vector>
#include "event2/event.h"

class license_check
{
private:
    bool _is_active; // 是否激活

    time_t _expiration_time; // 过期时间 UTC
    int _client_limit;       // 允许用户上限
    int _server_limit;       // 允许基站上限

    std::string _register_str;
    std::string _license_str;
    std::string _prev_license_str;

public:
    license_check(/* args */);
    ~license_check();

    std::string gen_register_file(std::string file_path = "register.lic"); // 生成注册文件
    int load_license_file(std::string file_path = "license.lic");          // 导入许可文件

    int fresh_license_file(); // 刷新许可数据

    int client_limit() { return _client_limit; };
    int server_limit() { return _server_limit; };
    time_t expiration_time() { return _expiration_time; };
    bool active() { return _is_active; };

    int enable_license_check(event_base *base);

private:
    std::string getMacAddress(const std::string &interfaceName);
    std::vector<std::string> getAllMacAddresses();
};
