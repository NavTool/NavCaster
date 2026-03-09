#pragma once
#include <string>
#include <unordered_map>
#include "event2/bufferevent.h"

class connect_bev
{
private:
    std::unordered_map<std::string, bufferevent *> _connect_map; // Connect_Key,bev

public:
    connect_bev(/* args */);
    ~connect_bev();
    

    bufferevent* find(std::string connect_key);
    int free(std::string connect_key);
    

    static connect_bev *getInstance();


};
