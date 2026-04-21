#include <spdlog/spdlog.h>
#include "ntrip_config.h"

#include "yaml-cpp/yaml.h"

int switch_Working_Dir(std::string exe_path)
{
    std::string exepath = exe_path;

    // 找到路径中最后一个斜杠的位置
    size_t lastSlashPos = exepath.find_last_of("/\\");
    if (lastSlashPos == std::string::npos)
    {
        spdlog::error("Unable to extract directory from executable path.");
        return 1;
    }
    // 提取路径
    std::string exeDir = exepath.substr(0, lastSlashPos);

    // 切换工作目录
#if defined(_MSC_VER)
    if (_chdir(exeDir.c_str()) != 0)
    {
        spdlog::error("Failed to change working directory.");
        return 1;
    }
#else
    if (chdir(exeDir.c_str()) != 0)
    {
        spdlog::error("Failed to change working directory.");
        return 1;
    }
#endif

    spdlog::info("Switch Working directory: {}", exeDir);
    return 0;
}

ntrip_config::ntrip_config(/* args */)
{
}

ntrip_config::~ntrip_config()
{
}

ntrip_config *ntrip_config::getInstance()
{
    static ntrip_config instance;
    return &instance;
}

int ntrip_config::Init(int argc, char **argv, std::string conf_path)
{
    // 解析输入：

    int listen_port = -1;

    if (argc < 1)
    {
        return 1;
    }

    if (argc == 2)
    {
        if (!strcmp(argv[1], "-info")) // 监听端口
        {
            exit(0);
        }
    }
    if (argc > 2)
    {
        for (int i = 1; i < argc; i += 2)
        {
            if (!strcmp(argv[i], "-port")) // 监听端口
            {
                listen_port = atoi(argv[i + 1]);
                spdlog::info("set listen port: {}", listen_port);
            }
            else if (!strcmp(argv[i], "-conf")) // 配置文件路径
            {
                conf_path = argv[i + 1];
                spdlog::info("set conf path: {}", conf_path);
            }
        }
    }

    // 根据传入的参数决定启动形式：

    switch_Working_Dir(argv[0]); // 切换工作路径到可执行目录下

    // 打开配置文件
    spdlog::info("Conf Path:{}", conf_path);
    // 读取全局配置
    spdlog::info("Load Conf...");

    // 没有传入参数，读取本地配置文件启动

    load_Caster_Conf(conf_path + "Service_Setting.yml");
    load_Core_Conf(conf_path + "Caster_Core.yml");
    load_Auth_Conf(conf_path + "Auth_Verify.yml");

    if (listen_port > 0)
    {
        ntrip_config::getInstance()->_listener_opt.set_listen_port(listen_port);
    }

    return 0;
}

int ntrip_config::load_Caster_Conf(std::string conf_file_path)
{
    YAML::Node Conf = YAML::LoadFile(conf_file_path);
    // std::cout << Conf << std::endl;

    // 配置转json
    auto Ntrip_Listener_Setting = Conf["Ntrip_Listener_Setting"];
    _listener_opt.set_listen_port(Ntrip_Listener_Setting["Listen_Port"].as<int>());
    _listener_opt.set_connect_timeout(Ntrip_Listener_Setting["Connect_Timeout"].as<int>());
    _listener_opt.set_enable_source_login(Ntrip_Listener_Setting["Enable_Source_Login"].as<bool>());
    _listener_opt.set_enable_server_login(Ntrip_Listener_Setting["Enable_Server_Login"].as<bool>());
    _listener_opt.set_enable_client_login(Ntrip_Listener_Setting["Enable_Client_Login"].as<bool>());
    _listener_opt.set_enable_nearest_login(Ntrip_Listener_Setting["Enable_Nearest_Login"].as<bool>());
    _listener_opt.set_enable_proxy_login(Ntrip_Listener_Setting["Enable_Proxy_Login"].as<bool>());
    _listener_opt.set_enable_alias_login(Ntrip_Listener_Setting["Enable_Alias_Login"].as<bool>());
    _listener_opt.set_enable_grid_login(Ntrip_Listener_Setting["Enable_Grid_Login"].as<bool>());
    _listener_opt.set_enable_header_no_crlf(Ntrip_Listener_Setting["Enable_Header_No_CRLF"].as<bool>());

    auto Server_Setting = Conf["Server_Setting"];
    _ntrip_server_opt.set_connect_timeout(Server_Setting["Connect_Timeout"].as<int>());
    _ntrip_server_opt.set_unsend_byte_limit(Server_Setting["Unsend_Byte_Limit"].as<int>());
    _ntrip_server_opt.set_heartbeat_interval(Server_Setting["Heart_Beat_Interval"].as<int>());
    _ntrip_server_opt.set_heartbeat_msg(Server_Setting["Heart_Beat_Msg"].as<std::string>());
    _ntrip_server_opt.set_decode_raw_data(Server_Setting["Decode_Raw_Data"].as<bool>());

    auto Client_Setting = Conf["Client_Setting"];
    _ntrip_client_opt.set_connect_timeout(Client_Setting["Connect_Timeout"].as<int>());
    _ntrip_client_opt.set_unsend_byte_limit(Client_Setting["Unsend_Byte_Limit"].as<int>());

    auto Common_Setting = Conf["Common_Setting"];
    _service_opt.set_refresh_state_interval(Common_Setting["Refresh_State_Interval"].as<int>());
    _service_opt.set_output_state(Common_Setting["Output_State"].as<bool>());

    auto Log_Setting = Conf["Log_Setting"];
    _service_opt.set_output_stdout(Log_Setting["Output_STD"].as<bool>());
    _service_opt.set_output_file(Log_Setting["Output_File"].as<bool>());
    _service_opt.set_output_file_daily(Log_Setting["Output_File_Daily"].as<bool>());
    _service_opt.set_output_file_hourly(Log_Setting["Output_File_Hourly"].as<bool>());
    _service_opt.set_output_file_rotate(Log_Setting["Output_File_Rotate"].as<bool>());
    _service_opt.set_file_rotate_size(Log_Setting["File_Rotating_Size"].as<int>());
    _service_opt.set_file_rotate_quata(Log_Setting["File_Rotating_Quata"].as<int>());
    _service_opt.set_file_save_path(Log_Setting["File_Save_Path"].as<std::string>());

    auto Debug_Mode = Conf["Debug_Mode"];
    _service_opt.set_cord_dump(Debug_Mode["Core_Dump"].as<bool>());
    _service_opt.set_output_debug_info(Debug_Mode["Output_Debug"].as<bool>());

    // HTTP API 设置
    if (Conf["HTTP_API_Setting"])
    {
        auto Http_Setting = Conf["HTTP_API_Setting"];
        if (Http_Setting["Port"])
            _http_api_config.port = Http_Setting["Port"].as<int>();
        if (Http_Setting["Bind_Addr"])
            _http_api_config.bind_addr = Http_Setting["Bind_Addr"].as<std::string>();
        if (Http_Setting["CORS_Origin"])
            _http_api_config.cors_origin = Http_Setting["CORS_Origin"].as<std::string>();
        if (Http_Setting["Admin_User"])
            _http_api_config.admin_user = Http_Setting["Admin_User"].as<std::string>();
        if (Http_Setting["Admin_Password"])
            _http_api_config.admin_password = Http_Setting["Admin_Password"].as<std::string>();
        if (Http_Setting["Web_Root"])
            _http_api_config.web_root = Http_Setting["Web_Root"].as<std::string>();
        if (Http_Setting["Force_Enable"])
            _http_api_config.force_enable = Http_Setting["Force_Enable"].as<bool>();
    }

    // Pass NTRIP listen port to HTTP API config for source table fetch
    _http_api_config.ntrip_port = _listener_opt.listen_port();

    return 0;
}

int ntrip_config::load_Core_Conf(std::string conf_file_path)
{
    YAML::Node Conf = YAML::LoadFile(conf_file_path);

    auto Caster_Setting = Conf["Caster_Setting"];
    _caster_core_opt.set_update_intv(Caster_Setting["Update_Intv"].as<int>());
    _caster_core_opt.set_key_expire_time(Caster_Setting["Key_Expire_Time"].as<int>());

    _caster_core_opt.set_upload_base_stat(Caster_Setting["Upload_Base_Stat"].as<bool>());
    _caster_core_opt.set_upload_rover_stat(Caster_Setting["Upload_Rover_Stat"].as<bool>());

    auto Base_Setting = Conf["Base_Setting"];
    _caster_core_opt.set_base_enable_mult(Base_Setting["Enable_Mult"].as<bool>());
    _caster_core_opt.set_base_keep_early(Base_Setting["Keep_Early"].as<bool>());

    auto Rover_Setting = Conf["Rover_Setting"];
    _caster_core_opt.set_rover_enable_mult(Rover_Setting["Enable_Mult"].as<bool>());
    _caster_core_opt.set_rover_keep_early(Rover_Setting["Keep_Early"].as<bool>());

    auto Notify_Setting = Conf["Notify_Setting"];
    _caster_core_opt.set_base_notify_inactive(Notify_Setting["Notify_Base_Inactive"].as<bool>());
    _caster_core_opt.set_rover_noify_inactive(Notify_Setting["Notify_Rover_Inactive"].as<bool>());

    auto Redis_Setting = Conf["Reids_Connect_Setting"];
    _caster_core_opt.set_redis_host(Redis_Setting["IP"].as<std::string>());
    _caster_core_opt.set_redis_port(Redis_Setting["Port"].as<int>());
    _caster_core_opt.set_redis_password(Redis_Setting["Requirepass"].as<std::string>());
    return 0;
}

int ntrip_config::load_Auth_Conf(std::string conf_file_path)
{
    YAML::Node Conf = YAML::LoadFile(conf_file_path);

    auto Base_Setting = Conf["Base_Setting"];
    _auth_verify_opt.set_base_anonymous_login(Base_Setting["Anonymous_Login"].as<bool>());
    _auth_verify_opt.set_base_online_protection(Base_Setting["Online_Protection"].as<bool>());

    auto Rover_Setting = Conf["Rover_Setting"];
    _auth_verify_opt.set_rover_anonymous_login(Rover_Setting["Anonymous_Login"].as<bool>());
    _auth_verify_opt.set_rover_anonymous_login(Rover_Setting["Online_Protection"].as<bool>());

    auto Source_Setting = Conf["Source_Setting"];
    _auth_verify_opt.set_source_anonymous_login(Source_Setting["Anonymous_Login"].as<bool>());

    auto Redis_Setting = Conf["Reids_Connect_Setting"];
    _auth_verify_opt.set_redis_host(Redis_Setting["IP"].as<std::string>());
    _auth_verify_opt.set_redis_port(Redis_Setting["Port"].as<int>());
    _auth_verify_opt.set_redis_password(Redis_Setting["Requirepass"].as<std::string>());
    return 0;
}

int ntrip_config::load_Conf_from_Center(std::string conf_center_addr, int port, std::string conf_center_auth)
{
    return 0;
}
