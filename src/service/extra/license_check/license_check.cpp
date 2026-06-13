#include "license_check.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#else
#include <ifaddrs.h>
#include <net/if.h>
#include <netpacket/packet.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

license_check::license_check()
{
    _primary_mac = pick_primary_mac();
    _machine_id  = license_codec::normalize_mac(_primary_mac);
}

int license_check::load_license_file(const std::string &license_file_path)
{
    _active = false;
    _info   = license_info{};

    std::ifstream infile(license_file_path);
    if (!infile)
        return -1;

    std::string content((std::istreambuf_iterator<char>(infile)), std::istreambuf_iterator<char>());
    // 去除空白字符
    content.erase(std::remove_if(content.begin(), content.end(),
                                 [](unsigned char c) { return std::isspace(c); }),
                  content.end());

    if (content.empty())
        return -2;

    if (!license_codec::decode(content, _primary_mac, _info))
        return -3;

    _active = !is_expired();
    return _active ? 0 : -4;
}

int license_check::write_register_file(const std::string &register_file_path)
{
    std::ofstream outfile(register_file_path);
    if (!outfile)
        return -1;
    outfile << _primary_mac << std::endl;
    return 0;
}

bool license_check::is_expired() const
{
    if (_info.expire_days == 0)
        return false; // 永不过期
    return license_codec::today_days() > _info.expire_days;
}

int license_check::remaining_days() const
{
    if (_info.expire_days == 0)
        return -1; // 永不过期
    int diff = static_cast<int>(_info.expire_days) - static_cast<int>(license_codec::today_days());
    return diff < 0 ? 0 : diff;
}

std::string license_check::expire_date() const
{
    if (_info.expire_days == 0)
        return "never";
    return license_codec::days_to_date(_info.expire_days);
}

// ---------------------------------------------------------------------------
// MAC 地址采集
// ---------------------------------------------------------------------------
#ifdef _WIN32
std::vector<std::string> license_check::get_all_macs()
{
    std::vector<std::string> macs;
    ULONG bufferSize = 0;
    GetAdaptersInfo(nullptr, &bufferSize);
    std::vector<BYTE> buffer(bufferSize);
    auto *adapterInfo = reinterpret_cast<IP_ADAPTER_INFO *>(buffer.data());
    if (GetAdaptersInfo(adapterInfo, &bufferSize) == ERROR_SUCCESS)
    {
        for (auto *p = adapterInfo; p; p = p->Next)
        {
            char macStr[18];
            std::snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
                          p->Address[0], p->Address[1], p->Address[2],
                          p->Address[3], p->Address[4], p->Address[5]);
            macs.emplace_back(macStr);
        }
    }
    return macs;
}
#else
std::vector<std::string> license_check::get_all_macs()
{
    std::vector<std::string> macs;
    struct ifaddrs *ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1)
        return macs;

    for (auto *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == nullptr) continue;
        if (ifa->ifa_addr->sa_family != AF_PACKET) continue;

        // 排除回环和无 MAC 接口
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;

        auto *s = reinterpret_cast<struct sockaddr_ll *>(ifa->ifa_addr);
        if (s->sll_halen != 6) continue;

        char buf[18];
        std::snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
                      s->sll_addr[0], s->sll_addr[1], s->sll_addr[2],
                      s->sll_addr[3], s->sll_addr[4], s->sll_addr[5]);
        macs.emplace_back(buf);
    }
    freeifaddrs(ifaddr);
    return macs;
}
#endif

std::string license_check::pick_primary_mac()
{
    auto macs = get_all_macs();
    for (const auto &m : macs)
    {
        if (m != "00:00:00:00:00:00")
            return m;
    }
    return "00:00:00:00:00:00";
}
