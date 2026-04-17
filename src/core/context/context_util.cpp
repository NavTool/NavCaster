#include "context_util.h"

int hexToDec(const std::string &hexStr)
{
    int value;
    std::stringstream ss;
    ss << std::hex << hexStr;
    ss >> value;
    return value;
}

void decodeKey(const std::string &key, std::string &serverIP, int &serverPort, std::string &clientIP, int &clientPort)
{
    if (key.size() != 24)
    {
        // Relay 连接使用随机 key，长度不是 24，无法解析 IP/端口
        serverIP.clear();
        serverPort = 0;
        clientIP.clear();
        clientPort = 0;
        return;
    }

    // 分离16进制字符串
    std::string hexIp1 = key.substr(0, 8);    // 服务器IP部分
    std::string hexPort1 = key.substr(8, 4);  // 服务器端口部分
    std::string hexIp2 = key.substr(12, 8);   // 客户端IP部分
    std::string hexPort2 = key.substr(20, 4); // 客户端端口部分

    // 解析服务器IP
    serverIP = std::to_string(hexToDec(hexIp1.substr(0, 2))) + "." +
               std::to_string(hexToDec(hexIp1.substr(2, 2))) + "." +
               std::to_string(hexToDec(hexIp1.substr(4, 2))) + "." +
               std::to_string(hexToDec(hexIp1.substr(6, 2)));

    // 解析服务器端口
    serverPort = hexToDec(hexPort1);

    // 解析客户端IP
    clientIP = std::to_string(hexToDec(hexIp2.substr(0, 2))) + "." +
               std::to_string(hexToDec(hexIp2.substr(2, 2))) + "." +
               std::to_string(hexToDec(hexIp2.substr(4, 2))) + "." +
               std::to_string(hexToDec(hexIp2.substr(6, 2)));

    // 解析客户端端口
    clientPort = hexToDec(hexPort2);
}

std::string convert_mount_info_to_string(mount_info i)
{
    std::string item;

    item = i.STR + ";" +
           i.mountpoint + ";" +
           i.identufier + ";" +
           i.format + ";" +
           i.format_details + ";" +
           i.carrier + ";" +
           i.nav_system + ";" +
           i.network + ";" +
           i.country + ";" +
           i.latitude + ";" +
           i.longitude + ";" +
           i.nmea + ";" +
           i.solution + ";" +
           i.generator + ";" +
           i.compr_encrryp + ";" +
           i.authentication + ";" +
           i.fee + ";" +
           i.bitrate + ";" +
           i.misc + ";" + "\r\n";

    return item;
}

mount_info build_default_mount_info(std::string mount_point)
{
    mount_info item = {
        "STR",
        mount_point,
        "unknown",
        "RTCM 3.3",
        "1074(1),1084(1),1094(1),1124(1)",
        "2",
        "GPS+GLO+GAL+BDS",
        "SNT",
        "XXX",
        "0.00",
        "0.00",
        "1",
        "0",
        "SNT",
        "none",
        "N",
        "N",
        "11520",
        "none"};

    return item;
}
