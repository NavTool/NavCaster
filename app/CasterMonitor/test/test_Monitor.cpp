#include "CasterMonitor.h"
#include "ServerDataController.h"
#include "event2/thread.h"
#include <qtimer.h>
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

#include "src/util.h"
#include "yaml-cpp/yaml.h"
#include <spdlog/spdlog.h>
#include <QGuiApplication>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QtQml/qqmlextensionplugin.h>

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

int main(int argc, char *argv[])
{
#ifdef WIN32
    evthread_use_windows_threads();
#else
    evthread_use_pthreads();
#endif
    // #ifdef WIN32
    //     WSADATA wsaData;
    //     if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    //     {
    //         spdlog::info("WSAStartup failed! exit.");
    //         return 1;
    //     }
    // #endif

    QGuiApplication app(argc, argv);


    QVariantMap redis_info;

    redis_info["ip"]="127.0.0.1";
    redis_info["port"]=16379;
    redis_info["auth"]="koro_redis";

    QString op_uid=CasterMonitor::getInstance()->addConnectCasterOperate(redis_info);


    CasterMonitor::getInstance()->excuteOperate(op_uid);




    QObject::connect(CasterMonitor::getInstance(),
                     &CasterMonitor::connectCasterSuccess,
                     CasterMonitor::getInstance(),
                     [=](){
                         QString   op_uid=  CasterMonitor::getInstance()->addRefreshServerOperate();
                         CasterMonitor::getInstance()->excuteOperate(op_uid); }
                     );


    return app.exec();




    // #ifdef WIN32
    //     WSACleanup();
    // #endif

}
