#include "relay_pull.h"

relay_pull::relay_pull(/* args */)
{
}

relay_pull::~relay_pull()
{
}

int relay_pull::start()
{

    // 创建连接

    // 根据类型创建不同的数据流

    // 如果是TCP Client


    // 如果是Ntrip Client ，创建一个TCP连接，连接建立完成，发送验证信息回调


    // 如果是TCP Server（先不支持） 创建一个listener,维护所有连接,所有的连接的ReadEvent都统一发送到指定频道，没有就不播发





    return 0;
}

int relay_pull::stop()
{
    return 0;
}

int relay_pull::runing()
{
    return 0;
}
