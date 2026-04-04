#pragma once
#include <google/protobuf/util/json_util.h>
#include <string>

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
