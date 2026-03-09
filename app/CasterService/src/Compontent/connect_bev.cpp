#include "connect_bev.h"

connect_bev::connect_bev()
{
}

connect_bev::~connect_bev()
{
}

bufferevent *connect_bev::find(std::string connect_key)
{
    auto con = _connect_map.find(connect_key);
    if (con == _connect_map.end())
    {
        return nullptr;
    }
    else
    {
        return con->second;
    }
}

int connect_bev::free(std::string connect_key)
{
    return 0;
}

connect_bev *connect_bev::getInstance()
{
    static connect_bev *instance = new connect_bev();
    return instance;
}
