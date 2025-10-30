#include "license_check.h"
#include "register.h"

#ifdef WIN32
#include <iphlpapi.h>
#include <windows.h>
#include <vector>
#pragma comment(lib, "iphlpapi.lib")
#else
#include <ifaddrs.h>
#include <net/if.h>
#include <netpacket/packet.h>
#endif

#include <vector>
#include <cstring>
#include <iomanip>
#include <cstdint>
#include <ctime>   // 包含 std::tm 和 std::mktime
#include <sstream> // 包含 std::istringstream
#include <fstream>

// 将日期字符串（格式：YYYY-MM-DD）转换为 UTC 时间戳（秒）
std::time_t convertStringToUTCSeconds(const std::string &dateStr)
{
    // 创建 tm 结构体
    std::tm timeStruct = {};

    // 解析字符串中的年、月、日
    int year, month, day;
    char dash1, dash2; // 用于存储 '-' 分隔符

    std::istringstream iss(dateStr);
    iss >> year >> dash1 >> month >> dash2 >> day;

    // 检查日期格式是否正确
    if (iss.fail() || dash1 != '-' || dash2 != '-')
    {
        // std::cerr << "日期格式不正确！应该为 YYYY-MM-DD" << std::endl;
        return -1;
    }

    // 设置 tm 结构体的年月日
    timeStruct.tm_year = year - 1900; // 年份需要减去 1900
    timeStruct.tm_mon = month - 1;    // 月份从 0 开始，减去 1
    timeStruct.tm_mday = day;         // 日

    // 设置时、分、秒为 0，表示当天的 00:00:00
    timeStruct.tm_hour = 0;
    timeStruct.tm_min = 0;
    timeStruct.tm_sec = 0;

    // 使用 mktime 将本地时间转换为时间戳
    std::time_t utcSeconds = std::mktime(&timeStruct);

    // 如果转换失败，返回 -1
    if (utcSeconds == -1)
    {
        // std::cerr << "时间转换失败！" << std::endl;
    }

    return utcSeconds;
}

license_check::license_check(/* args */)
{
    _client_limit = 10;
    _server_limit = 10;
    _expiration_time = 9999999999;
}

license_check::~license_check()
{
}

std::string license_check::gen_register_file(std::string file_path)
{
    auto macs=getAllMacAddresses();
    _register_str="00:00:00:00:00:00";
    for(auto iter:macs)
    {
        if(iter !="00:00:00:00:00:00")
        {
            _register_str=iter;
        }
    }
    // _register_str = getMacAddress("eth0");
    CRegister reg;
    auto reg_str = reg.genMachineCode(_register_str.substr(6));

    std::ofstream outfile(file_path);
    if (outfile)
    {
        outfile << reg_str << std::endl;
        outfile.close();
    }

    return reg_str;
}

int license_check::load_license_file(std::string file_path)
{
    std::ifstream infile(file_path);

    if (infile)
    {
        std::string fileContent((std::istreambuf_iterator<char>(infile)),
                                std::istreambuf_iterator<char>());
        _license_str = fileContent;
    }

    fresh_license_file();

    return 0;
}

int license_check::fresh_license_file()
{
    if (_license_str != _prev_license_str)
    {
        _prev_license_str = _license_str;

        // 检查许可信息
        CRegister reg;

        if (reg.checkRegest(_register_str, _prev_license_str))
        {
            _is_active = true;
            _client_limit = reg.m_client_limit;
            _server_limit = reg.m_server_limit;
            _expiration_time = convertStringToUTCSeconds(reg.m_endTime);
        }
        else
        {
            _is_active = false;
            _client_limit = 10;
            _server_limit = 10;
            _expiration_time = 9999999999;
        }

        // 更新许可信息
    }
    return 0;
}

int license_check::enable_license_check(event_base *base)
{
    return 0;
}

// 获取网卡的MAC地址
#ifdef WIN32
std::string license_check::getMacAddress(const std::string &interfaceName)
{
    ULONG bufferSize = 0;
    GetAdaptersInfo(nullptr, &bufferSize); // 获取所需的缓冲区大小

    std::vector<BYTE> buffer(bufferSize);
    IP_ADAPTER_INFO *adapterInfo = reinterpret_cast<IP_ADAPTER_INFO *>(buffer.data());

    if (GetAdaptersInfo(adapterInfo, &bufferSize) == ERROR_SUCCESS)
    {
        char macStr[18];
        std::snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                      adapterInfo->Address[0], adapterInfo->Address[1], adapterInfo->Address[2],
                      adapterInfo->Address[3], adapterInfo->Address[4], adapterInfo->Address[5]);
        return std::string(macStr);
    }

    // std::cerr << "Adapter not found or failed to retrieve MAC address." << std::endl;
    return std::string();
}
#else
std::string license_check::getMacAddress(const std::string &interfaceName)
{
    struct ifaddrs *ifaddr, *ifa;
    char mac_address[18] = {0}; // 用于存储MAC地址的缓冲区

    // 获取系统中所有接口的地址
    if (getifaddrs(&ifaddr) == -1)
    {
        perror("getifaddrs");
        return "";
    }

    // 遍历接口列表
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL)
            continue;

        // 检查接口是否是AF_PACKET类型（用于获取MAC地址）并且接口名称匹配
        if (ifa->ifa_addr->sa_family == AF_PACKET && strcmp(ifa->ifa_name, interfaceName.c_str()) == 0)
        {
            struct sockaddr_ll *s = (struct sockaddr_ll *)ifa->ifa_addr;
            snprintf(mac_address, sizeof(mac_address), "%02x:%02x:%02x:%02x:%02x:%02x",
                     s->sll_addr[0], s->sll_addr[1], s->sll_addr[2],
                     s->sll_addr[3], s->sll_addr[4], s->sll_addr[5]);
            break; // 找到匹配的接口后跳出循环
        }
    }

    freeifaddrs(ifaddr); // 释放地址结构
    return std::string(mac_address);
}
#endif

#ifdef WIN32
std::vector<std::string> license_check::getAllMacAddresses()
{
    ULONG bufferSize = 0;
    GetAdaptersInfo(nullptr, &bufferSize); // 获取所需的缓冲区大小

    std::vector<BYTE> buffer(bufferSize);
    IP_ADAPTER_INFO *adapterInfo = reinterpret_cast<IP_ADAPTER_INFO *>(buffer.data());

    std::vector<std::string> macAddresses;

    if (GetAdaptersInfo(adapterInfo, &bufferSize) == ERROR_SUCCESS)
    {
        IP_ADAPTER_INFO *pAdapter = adapterInfo;
        while (pAdapter)
        {
            char macStr[18];
            std::snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                          pAdapter->Address[0], pAdapter->Address[1], pAdapter->Address[2],
                          pAdapter->Address[3], pAdapter->Address[4], pAdapter->Address[5]);
            macAddresses.push_back(std::string(macStr));
            pAdapter = pAdapter->Next;
        }
    }

    return macAddresses;
}
#else
std::vector<std::string> license_check::getAllMacAddresses()
{
    struct ifaddrs *ifaddr, *ifa;
    std::vector<std::string> macAddresses;

    // 获取系统中所有接口的地址
    if (getifaddrs(&ifaddr) == -1)
    {
        perror("getifaddrs");
        return macAddresses; // 如果获取失败，返回空列表
    }

    // 遍历接口列表
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL)
            continue;

        // 检查接口是否是AF_PACKET类型
        if (ifa->ifa_addr->sa_family == AF_PACKET)
        {
            struct sockaddr_ll *s = (struct sockaddr_ll *)ifa->ifa_addr;
            char mac_address[18];
            snprintf(mac_address, sizeof(mac_address), "%02x:%02x:%02x:%02x:%02x:%02x",
                     s->sll_addr[0], s->sll_addr[1], s->sll_addr[2],
                     s->sll_addr[3], s->sll_addr[4], s->sll_addr[5]);
            macAddresses.push_back(std::string(mac_address));
        }
    }

    freeifaddrs(ifaddr); // 释放地址结构
    return macAddresses;
}
#endif