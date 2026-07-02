#pragma once
#include <string>
#include <deque>
#include <ctime>
#include <sstream>
#include <stdexcept>
#include <Caster_Core.h>

#include "knt.h"
#include "proto_json.h"

#include "core/AccessGroup.pb.h"
#include "core/AccessItem.pb.h"
#include "core/AliasRule.pb.h"
#include "core/BroadcastMsg.pb.h"
#include "core/CasterNode.pb.h"
#include "core/ClientState.pb.h"
#include "core/PullRecord.pb.h"
#include "core/PullState.pb.h"
#include "core/PushRecord.pb.h"
#include "core/PushState.pb.h"
#include "core/ServerState.pb.h"
#include "core/SourceRecord.pb.h"
#include "core/StreamState.pb.h"

// 将十六进制字符串解析为十进制整数
int hexToDec(const std::string &hexStr);

// 从16进制字符串还原IP和端口
void decodeKey(const std::string &key, std::string &serverIP, int &serverPort, std::string &clientIP, int &clientPort);

// 将mount_info转换为NTRIP源列表格式字符串
std::string convert_mount_info_to_string(mount_info i);

// 构建默认的挂载点信息
mount_info build_default_mount_info(std::string mount_point);