#include "MonitorCore.h"

MonitorCore *MonitorCore::getInstance()
{
    static MonitorCore *instance = new MonitorCore();
    return instance;
}

int MonitorCore::start()
{
    // 初始化 Caster模块

    // 初始化 Init模块


    // 定时器事件，定时从两个模块中拉取数据到本地加锁内存中，



    //






    return 0;
}
