#include "MonitorCore.h"

#if defined(__GNUC__)
// GCC 编译器相关的代码
#elif defined(_MSC_VER)
// Visual Studio 编译器相关的代码
// #define YAML_CPP_STATIC_DEFINE
#include <direct.h> // 包含 _chdir 函数的头文件
#elif defined(__clang__)
// Clang 编译器相关的代码
#else
// 其他编译器的代码
#endif

#include "yaml-cpp/yaml.h"
#include <spdlog/spdlog.h>

#define CONF_PATH "conf/"

json load_Core_Conf(const char *conf_directory)
{
    json conf;
    std::string Path = conf_directory;
    YAML::Node Conf = YAML::LoadFile(Path + "Caster_Core.yml");

    auto Caster_Setting = Conf["Caster_Setting"];
    conf["Update_Intv"] = Caster_Setting["Update_Intv"].as<int>();
    conf["Unactive_Time"] = Caster_Setting["Unactive_Time"].as<int>();
    conf["Key_Expire_Time"] = Caster_Setting["Key_Expire_Time"].as<int>();

    conf["Upload_Base_Stat"] = Caster_Setting["Upload_Base_Stat"].as<bool>();
    conf["Upload_Rover_Stat"] = Caster_Setting["Upload_Rover_Stat"].as<bool>();
    conf["Download_Base_Stat"] = Caster_Setting["Download_Base_Stat"].as<bool>();
    conf["Download_Rover_Stat"] = Caster_Setting["Download_Rover_Stat"].as<bool>();

    auto Base_Setting = Conf["Base_Setting"];
    conf["Base_Enable_Mult"] = Base_Setting["Enable_Mult"].as<bool>();
    conf["Base_Keep_Early"] = Base_Setting["Keep_Early"].as<bool>();

    auto Rover_Setting = Conf["Rover_Setting"];
    conf["Rover_Enable_Mult"] = Rover_Setting["Enable_Mult"].as<bool>();
    conf["Rover_Keep_Early"] = Rover_Setting["Keep_Early"].as<bool>();

    auto Notify_Setting = Conf["Notify_Setting"];
    conf["Notify_Base_Inactive"]=Notify_Setting["Notify_Base_Inactive"].as<bool>();
    conf["Notify_Rover_Inactive"]=Notify_Setting["Notify_Rover_Inactive"].as<bool>();

    auto Redis_Setting = Conf["Reids_Connect_Setting"];
    conf["Redis_IP"] = Redis_Setting["IP"].as<std::string>();
    conf["Redis_Port"] = Redis_Setting["Port"].as<int>();
    conf["Redis_Requirepass"] = Redis_Setting["Requirepass"].as<std::string>();

    return conf;
}

json load_Auth_Conf(const char *conf_directory)
{
    json conf;
    std::string Path = conf_directory;
    YAML::Node Conf = YAML::LoadFile(Path + "Auth_Verify.yml");

    auto Redis_Setting = Conf["Reids_Connect_Setting"];
    conf["Redis_IP"] = Redis_Setting["IP"].as<std::string>();
    conf["Redis_Port"] = Redis_Setting["Port"].as<int>();
    conf["Redis_Requirepass"] = Redis_Setting["Requirepass"].as<std::string>();

    return conf;
}



static void task_sleepms(uint32_t milliseconds)
{
#if defined(_WIN32) || defined(_WIN64)
    ::Sleep(milliseconds);  // Windows 的 Sleep 单位是毫秒
#else
    ::usleep(static_cast<useconds_t>(milliseconds) * 1000);  // Linux 的 usleep 单位是微秒
#endif
}



int main()
{
#ifdef WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        spdlog::info("WSAStartup failed! exit.");
        return 1;
    }
#endif

    std::string conf_path = CONF_PATH;

    json cfg;

    cfg["Core_Setting"] = load_Core_Conf(conf_path.c_str());
    cfg["Auth_Setting"] = load_Auth_Conf(conf_path.c_str());



    MonitorCore a;

    a._auth_verify_setting=cfg["Auth_Setting"];
    a._caster_core_setting=cfg["Core_Setting"];

    a.start();



    while(1)
    {





        task_sleepms(1000);
    }





#ifdef WIN32
    WSACleanup();
#endif


    return 0;
}
