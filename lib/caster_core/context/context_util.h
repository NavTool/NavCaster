#pragma once
#include <google/protobuf/util/json_util.h>
#include <string>
#include <deque>
#include <ctime>

#include "knt.h"







#include "core/AccessGroup.pb.h"
#include "core/AccessItem.pb.h"
#include "core/AliasRule.pb.h"
#include "core/BoardcastMsg.pb.h"
#include "core/CasterNode.pb.h"
#include "core/ClientState.pb.h"
#include "core/PullRecord.pb.h"
#include "core/PullState.pb.h"
#include "core/PushRecord.pb.h"
#include "core/PushState.pb.h"
#include "core/ServerState.pb.h"
#include "core/SourceRecord.pb.h"
#include "core/StreamState.pb.h"

template <typename T>
std::string ProtoToJson(const T &msg)
{
    google::protobuf::json::PrintOptions opt;
    opt.add_whitespace = true;                       // 转换成json是否添加空格、换行和缩进
    opt.always_print_fields_with_no_presence = true; // 打印不支持存在的字段
    opt.always_print_enums_as_ints = false;          // 将枚举类型打印为int
    opt.preserve_proto_field_names = true;           // 是否保留原型字段名
    opt.unquote_int64_if_possible = true;            // 关键
    std::string json_str;
    auto res = google::protobuf::json::MessageToJsonString(msg, &json_str, opt);

    if (res.ok())
    {
        return json_str;
    }
    else
    {
        return std::string();
    }
}

template <typename T>
bool JsonToProto(const std::string &json, T &msg)
{
    google::protobuf::json::ParseOptions opt;
    opt.ignore_unknown_fields = true; // 关键：向前 / 向后兼容

    auto status = google::protobuf::json::JsonStringToMessage(json, &msg, opt);
    return status.ok();
}


// 将十六进制字符串解析为十进制整数
inline int hexToDec(const std::string &hexStr)
{
    int value;
    std::stringstream ss;
    ss << std::hex << hexStr;
    ss >> value;
    return value;
}

// 从16进制字符串还原IP和端口
inline void decodeKey(const std::string &key, std::string &serverIP, int &serverPort, std::string &clientIP, int &clientPort)
{
    if (key.size() != 24)
    {
        throw std::invalid_argument("Invalid key length");
    }

    // 如果使用的时IPv6那要如何支持呢

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

inline std::string convert_mount_info_to_string(mount_info i)
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

inline mount_info build_default_mount_info(std::string mount_point)
{
    // STR;              STR;
    // mountpoint;       KORO996;
    // identufier;       ShangHai;
    // format;           RTCM 3.3;
    // format-details;   1004(5),1074(1),1084(1),1094(1),1124(1)
    // carrier;          2
    // nav-system;       GPS+GLO+GAL+BDS
    // network;          KNT
    // country;          CHN
    // latitude;         36.11
    // longitude;        120.11
    // nmea;             0
    // solution;         0
    // generator;        SN
    // compr-encrryp;    none
    // authentication;   B
    // fee;              N
    // bitrate;          9100
    // misc;             caster.koroyo.xyz:2101/KORO996

    // mount_info item = {
    //     "STR",
    //     mount_point,
    //     "unknown",
    //     "unknown",
    //     "unknown",
    //     "0",
    //     "unknown",
    //     "unknown",
    //     "unknown",
    //     "00.00",
    //     "000.00",
    //     "0",
    //     "0",
    //     "unknown",
    //     "unknown",
    //     "B",
    //     "N",
    //     "0000",
    //     "Not parsed or provided"};

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