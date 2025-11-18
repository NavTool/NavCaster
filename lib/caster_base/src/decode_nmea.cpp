#pragma once
#include <string>

class rtcm_decode
{
private:
    /* data */

public:
    rtcm_decode(/* args */);
    ~rtcm_decode();

    int add_datas(const char *data, size_t length);
};
