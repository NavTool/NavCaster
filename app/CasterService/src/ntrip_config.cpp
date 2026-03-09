#include "ntrip_config.h"

#include "yaml-cpp/yaml.h"

ntrip_config::ntrip_config(/* args */)
{
}

ntrip_config::~ntrip_config()
{
}

ntrip_config *ntrip_config::getInstance()
{
    static ntrip_config *instance = new ntrip_config();
    return instance;
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
    _listener_opt.set_enable_nearest_login(Ntrip_Listener_Setting["Enable_Nearest_MPT"].as<bool>());
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
    auto Redis_Setting = Conf["Reids_Connect_Setting"];
    _auth_verify_opt.set_redis_host(Redis_Setting["IP"].as<std::string>());
    _auth_verify_opt.set_redis_port(Redis_Setting["Port"].as<int>());
    _auth_verify_opt.set_redis_password(Redis_Setting["Requirepass"].as<std::string>());
    return 0;
}
