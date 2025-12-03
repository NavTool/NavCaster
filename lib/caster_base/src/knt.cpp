#include "knt.h"
#include <iostream>
#include <ctime>
#include <iomanip>
#include <chrono>


// #include <unistd.h>
#include <sstream>
#ifdef WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#include <iostream>
#include <windows.h>
#include <psapi.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <sys/resource.h>
#endif


#include <stdint.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <mstcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif



#include <random>

std::string util_random_string(int string_len)
{
    std::string rand_str;

    std::random_device rd;  // non-deterministic generator
    std::mt19937 gen(rd()); // to seed mersenne twister.

    for (int i = 0; i < string_len; i++)
    {
        switch (gen() % 3)
        {
        case 0:
            rand_str += gen() % 26 + 'a';
            break;
        case 1:
            rand_str += gen() % 26 + 'A';
            break;
        case 2:
            rand_str += gen() % 10 + '0';
            break;

        default:
            break;
        }
    }

    return rand_str;
}

std::string util_cal_connect_key(const char *ServerIP, int serverPort, const char *ClientIP, int clientPort)
{
    std::string base = "0123456789ABCDEF"; // 定义16进制表示的基本符号集合

    char hexIp1[9] = "", hexIp2[9] = ""; // 存放转换后的十六进制字符串
    char hexPort1[5] = "", hexPort2[5] = "";

    int octets1[4]; // IPv4地址由四个八位组成
    sscanf(ServerIP, "%d.%d.%d.%d", &octets1[0], &octets1[1], &octets1[2], &octets1[3]);

    int i = 0;
    for (i = 0; i < 4; i++)
    {
        snprintf(hexIp1 + i * 2, sizeof(hexIp1) - i * 2, "%02X", static_cast<unsigned>(octets1[i]));
    }

    int octets2[4]; // IPv4地址由四个八位组成
    sscanf(ClientIP, "%d.%d.%d.%d", &octets2[0], &octets2[1], &octets2[2], &octets2[3]);

    for (i = 0; i < 4; i++)
    {
        snprintf(hexIp2 + i * 2, sizeof(hexIp2) - i * 2, "%02X", static_cast<unsigned>(octets2[i]));
    }

    snprintf(hexPort1, sizeof(hexPort1), "%04X", serverPort);
    snprintf(hexPort2, sizeof(hexPort2), "%04X", clientPort);

    char key[25] = "";

    snprintf(key, sizeof(key), "%s%s%s%s", hexIp1, hexPort1, hexIp2, hexPort2);

    return std::string(key);
}

std::string util_cal_half_key(const char *IP, int Port)
{
    std::string base = "0123456789ABCDEF"; // 定义16进制表示的基本符号集合

    char hexIp[9] = "";
    char hexPort[5] = "";

    int octets1[4]; // IPv4地址由四个八位组成
    sscanf(IP, "%d.%d.%d.%d", &octets1[0], &octets1[1], &octets1[2], &octets1[3]);

    for (int i = 0; i < 4; ++i)
    {
        snprintf(hexIp + i * 2, sizeof(hexIp), "%02X", static_cast<unsigned>(octets1[i]));
    }
    snprintf(hexPort, sizeof(hexPort), "%04X", Port);
    char key[25] = "";

    sprintf(key, "%s%s", hexIp, hexPort);
    return std::string() = key;
}

std::string util_cal_connect_key(int fd)
{
    std::string ServerIP, ClientIP;
    int ServerPort, ClientPort;

    // // 获取本地端点信息
    // sockaddr_in localAddress;
    // int localAddressLength = sizeof(localAddress);
    // getsockname(fd, reinterpret_cast<sockaddr*>(&localAddress), &localAddressLength);
    // char localIP[INET_ADDRSTRLEN];
    // inet_ntop(AF_INET, &localAddress.sin_addr, localIP, INET_ADDRSTRLEN);
    // //std::cout << "Local IP: " << localIP << ", Port: " << ntohs(localAddress.sin_port) << std::endl;

    // ServerIP=localIP;
    // ServerPort=ntohs(localAddress.sin_port);

    // // 获取远程端点信息
    // sockaddr_in remoteAddress;
    // int remoteAddressLength = sizeof(remoteAddress);
    // getpeername(fd, reinterpret_cast<sockaddr*>(&remoteAddress), &remoteAddressLength);
    // char remoteIP[INET_ADDRSTRLEN];
    // inet_ntop(AF_INET, &remoteAddress.sin_addr, remoteIP, INET_ADDRSTRLEN);
    // //std::cout << "Remote IP: " << remoteIP << ", Port: " << ntohs(remoteAddress.sin_port) << std::endl;

    // ClientIP=remoteIP;
    // ClientPort=ntohs(remoteAddress.sin_port);

    struct sockaddr_in sa1;
    socklen_t len1 = sizeof(sa1);
    if (getsockname(fd, (struct sockaddr *)&sa1, &len1))
    {
        return std::string();
    }

    struct sockaddr_in sa2;
    socklen_t len2 = sizeof(sa2);
    if (getpeername(fd, (struct sockaddr *)&sa2, &len2))
    {
        return std::string();
    }
    ServerIP = inet_ntoa(sa1.sin_addr);
    ServerPort = ntohs(sa1.sin_port);
    ClientIP = inet_ntoa(sa2.sin_addr);
    ClientPort = ntohs(sa2.sin_port);

    return util_cal_connect_key(ServerIP.c_str(), ServerPort, ClientIP.c_str(), ClientPort);
}

std::string util_port_to_key(int port)
{
    std::string base = "0123456789ABCDEF"; // 定义16进制表示的基本符号集合

    char key[5] = "";

    snprintf(key, sizeof(key), "%04X", port);
    // if(key.size())

    return std::string() = key;
}

std::string util_get_user_ip(int fd)
{
    struct sockaddr_in sa2;
    socklen_t len2 = sizeof(sa2);
    if (getpeername(fd, (struct sockaddr *)&sa2, &len2))
    {
        return std::string();
    }

    std::string Clientip = inet_ntoa(sa2.sin_addr);
    int clientport = ntohs(sa2.sin_port);

    return Clientip;
}
int util_get_user_port(int fd)
{
    struct sockaddr_in sa2;
    socklen_t len2 = sizeof(sa2);
    if (getpeername(fd, (struct sockaddr *)&sa2, &len2))
    {
        return 0;
    }
    std::string Clientip = inet_ntoa(sa2.sin_addr);
    int clientport = ntohs(sa2.sin_port);

    return clientport;
}
std::string util_get_date_time()
{

    time_t now = time(0);                 // 获取当前时间的time_t类型值
    struct tm *tm_info = localtime(&now); // 将time_t类型值转换为struct tm类型的本地时间信息
    char buffer[80];                      // 存放格式化后的日期时间字符串
    strftime(buffer, sizeof(buffer), "%Y/%m/%d %H:%M:%S", tm_info);

    std::string tm = buffer;

    return tm;
}
std::string util_get_space_time()
{
    return std::string("0000/00/00 00:00:00");
}
std::string util_get_http_date()
{
    std::time_t now = std::time(nullptr);
    std::tm *gmt = std::gmtime(&now);

    std::ostringstream oss;
    oss << std::put_time(gmt, "%a, %d %b %Y %H:%M:%S GMT");
    return oss.str();

    return std::string();
}

std::time_t util_get_now_second()
{
    // 获取当前时间点
    auto now = std::chrono::system_clock::now();
    // 转换为 time_t 类型（自1970年1月1日以来的秒数）
    std::time_t current_time = std::chrono::system_clock::to_time_t(now);

    return current_time;
}
int util_get_use_memory()
{

#ifdef WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc;
    SIZE_T virtualMemUsedByMe;
    SIZE_T physicalMemUsedByMe;
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof(pmc)))
    {
        virtualMemUsedByMe = pmc.PrivateUsage;    // 当前进程使用的虚拟内存大小
        physicalMemUsedByMe = pmc.WorkingSetSize; // 当前进程使用的物理内存大小

        // std::cout << "Virtual Memory Used: " << virtualMemUsedByMe / (1024 * 1024) << " MB" << std::endl;
        // std::cout << "Physical Memory Used: " << physicalMemUsedByMe / (1024 * 1024) << " MB" << std::endl;
    }
    else
    {
        virtualMemUsedByMe = 0;  // 当前进程使用的虚拟内存大小
        physicalMemUsedByMe = 0; // 当前进程使用的物理内存大小
        // std::cerr << "GetProcessMemoryInfo failed\n";
    }
    return virtualMemUsedByMe; //

#else
    struct rusage usage;
    // 调用 getrusage() 函数获取当前进程的资源使用情况
    if (getrusage(RUSAGE_SELF, &usage) == -1)
    {
        // std::cerr << "Failed to retrieve resource usage." << std::endl;
        return 0;
    }

    // 输出程序占用的物理内存大小（单位为字节）
    // std::cout << "Memory used by the program in bytes: " << usage.ru_maxrss * 1024 << std::endl;

    return usage.ru_maxrss; // KB
#endif
}

void util_ecef2pos(double x, double y, double z,
                   double &lat, double &lon, double &alt)
{
    const double a = 6378137.0;
    const double f = 1.0 / 298.257223563;
    const double e2 = f * (2 - f);

    lon = atan2(y, x);

    double p = sqrt(x * x + y * y);
    double theta = atan2(z, p * (1 - f));
    double sinT = sin(theta), cosT = cos(theta);

    // 初始纬度
    lat = atan2(z + e2 * (1 - f) * a * sinT * sinT * sinT,
                p - e2 * a * cosT * cosT * cosT);

    double lat_prev;
    double sinLat, N;

    // Newton 迭代（2~3次）
    do
    {
        lat_prev = lat;
        sinLat = sin(lat);
        N = a / sqrt(1 - e2 * sinLat * sinLat);
        lat = atan2(z + e2 * N * sinLat, p);
    } while (fabs(lat - lat_prev) > 1e-14);

    alt = p / cos(lat) - N;

    // 转换为度
    const double PI = 3.14159265358979323846;
    lat *= 180.0 / PI;
    lon *= 180.0 / PI;
}

void util_pos2ecef(double lat, double lon, double alt,
                   double &x, double &y, double &z)
{
    const double a = 6378137.0;
    const double f = 1.0 / 298.257223563;
    const double e2 = f * (2 - f);
    const double PI = 3.14159265358979323846;

    lat *= PI / 180.0;
    lon *= PI / 180.0;

    double sinLat = sin(lat), cosLat = cos(lat);
    double sinLon = sin(lon), cosLon = cos(lon);

    double N = a / sqrt(1 - e2 * sinLat * sinLat);

    x = (N + alt) * cosLat * cosLon;
    y = (N + alt) * cosLat * sinLon;
    z = (N * (1 - e2) + alt) * sinLat;
}

long long util_get_time_stamp()
{
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    // 转换为时间类型
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    // 获取秒数
    std::chrono::seconds seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
    long long seconds_count = seconds.count();
    return seconds_count;
}

std::string util_get_time_stamp_str()
{
    return std::to_string(util_get_time_stamp());
}

double util_dist3d(double x1, double y1, double z1, double x2, double y2, double z2)
{
    double dx = x1 - x2;
    double dy = y1 - y2;
    double dz = z1 - z2;

    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::string util_generate_random_key(int length)
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15); // 生成十六进制数

    // // 获取时间戳和线程ID作为一部分
    // auto time_now = std::chrono::steady_clock::now().time_since_epoch().count();
    // auto thread_id = std::this_thread::get_id();

    std::ostringstream oss;
    // // 使用时间戳和线程ID增加唯一性
    // oss << std::hex <<thread_id << "-" <<time_now;

    // 随机生成附加的16进制字符，增加随机性
    for (size_t i = oss.str().size(); i < length; ++i)
    { // 保证生成指定长度的key
        oss << std::hex << dis(gen);
    }
    return oss.str();
}

int64_t util_get_tcp_delay(util_socket_t sockfd)
{
    #if defined(_WIN32)

    // Windows 平台
    TCP_INFO_v0 info;
    DWORD bytes = sizeof(info);  // 必须是 sizeof(TCP_INFO_v0)


    if (WSAIoctl(
            sockfd,
            SIO_TCP_INFO,
            nullptr, 0,
            &info, sizeof(info),
            &bytes,
            nullptr, nullptr) != 0)
    {
        int err = WSAGetLastError();
        return 0;
    }

    // info.RttUs 已经是微秒
    return static_cast<int64_t>(info.RttUs);

#else

    // Linux 平台
    struct tcp_info info;
    socklen_t len = sizeof(info);

    if (getsockopt(sockfd, IPPROTO_TCP, TCP_INFO, &info, &len) != 0)
    {
        return -1;
    }

    // tcpi_rtt：微秒
    return static_cast<int64_t>(info.tcpi_rtt);

#endif
}


