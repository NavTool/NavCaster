#pragma once
#include <string>
#include <iostream>
#include <type_traits>
#include <QObject>
#include <QtQml/qqml.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;





enum class StationType
{
    UNKNOWN = 0,
    Static = 1,
    Dynamic = 2
};



enum class SolveMode
{
    UNKNOWN=0,
    SPP=1,
    PVT=2,
    PPP=3,
    SBAS=4,
    RTK=5,
    STATIC=6
};

//坐标类型（适用于静态站点）
enum class CoordType
{
    UNKNOWN = 0,
    File_Header=1, // 从文件头获取的坐标
    Sacn=2,        // 快速获取的坐标（最后一百个历元的单点定位平均值）
    SPP_AVERAGE=3, // 单点定位坐标平均值
    PPP_AVERAGE=4, // PPP坐标
    STATIC=5,      // 静态基线解算
    Adjust=6,      // 平差后的坐标
    FreeAdjust=7,  // 秩亏自由网平差
    Custom=8,      // 用户输入
    Saved=9       // 保存的坐标
};



enum class SolveConfig
{
    UNKNOWN=0,
    SINO_PVT0=1001,
    SINO_PVT1=1002,
    SINO_PVT2=1003,
    SINO_PVT3=1004,
    SINO_PVT4=1005,

    PENA_PVT0=1101,
    PENA_PVT1=1102,
    PENA_PVT2=1103,
    PENA_PVT3=1104,
    PENA_PVT4=1105,

    XW_PVT0=1201,


    SINO_PPP0=1301,
    SINO_PPP1=1302,
    SINO_PPP2=1303,
    SINO_PPP3=1304,

    SPP_MODE=2001

};




// 判断类型对应的 is_* 函数
template <typename T>
struct JsonTypeChecker;

template <>
struct JsonTypeChecker<std::string>
{
    static bool check(const nlohmann::json &json, const std::string &key)
    {
        return json.contains(key) && json[key].is_string();
    }
};

template <>
struct JsonTypeChecker<int>
{
    static bool check(const nlohmann::json &json, const std::string &key)
    {
        return json.contains(key) && json[key].is_number_integer();
    }
};

template <>
struct JsonTypeChecker<long long>
{
    static bool check(const nlohmann::json &json, const std::string &key)
    {
        return json.contains(key) && json[key].is_number_integer();
    }
};

#ifdef WIN32

#else
template <>
struct JsonTypeChecker<time_t>
{
    static bool check(const nlohmann::json &json, const std::string &key)
    {
        return json.contains(key) && json[key].is_number_integer();
    }
};
#endif

template <>
struct JsonTypeChecker<double>
{
    static bool check(const nlohmann::json &json, const std::string &key)
    {
        return json.contains(key) && (json[key].is_number_float() || json[key].is_number_integer());
    }
};

template <>
struct JsonTypeChecker<bool>
{
    static bool check(const nlohmann::json &json, const std::string &key)
    {
        return json.contains(key) && json[key].is_boolean();
    }
};

template <>
struct JsonTypeChecker<nlohmann::json>
{
    static bool check(const nlohmann::json &json, const std::string &key)
    {
        return json.contains(key) && json[key].is_object();
    }
};

// 检查 JSON 是否包含指定的枚举值的工具类
/*
 *  兼容性改正：
 *  在实际调试过程中发现qml中应用的枚举值会被转换成double类型的值（typeof()得到类型为number），
 *  原因应该是在qml中，所有的数字都被描述为浮点类型，进而导致传递给json的值是float类型
 *  导致枚举如果按照int类型进行转换的时候失效，无法获取到值，
 *  该问题没有想到很好的解决方法，因为有些枚举类型传递的还是int，可能是跟这个值没有被修改有关，
 *  也可能是qml部分代码不规范导致的
 *
 *  因此在check函数中放宽了限制，支持float类型的值转换成枚举类型
 *  并在PROPERTY_ENUM从json赋值到枚举值的过程中，将json的值统一转换成double，然后使用std::round进行四舍五入
 *
 */
template <typename EnumType>
struct EnumJsonChecker
{
    static_assert(std::is_enum<EnumType>::value, "EnumType must be an enum type.");

    static bool check(const nlohmann::json &json, const std::string &key)
    {
        // return json.contains(key) && json[key].is_number_integer();
        return json.contains(key) && (json[key].is_number_integer() || json[key].is_number_float());
    }
};

// 枚举类型专用的宏
#define PROPERTY_ENUM(ENUM_TYPE, NAME)                                              \
public:                                                                             \
    bool NAME(const nlohmann::json &json, const std::string &key)                   \
    {                                                                               \
        if (EnumJsonChecker<ENUM_TYPE>::check(json, key))                           \
        {                                                                           \
            m_##NAME = static_cast<ENUM_TYPE>(std::round(json[key].get<double>())); \
            return true;                                                            \
        }                                                                           \
        else                                                                        \
        {                                                                           \
            std::cerr << "Failed to set [" #NAME "] from key: " << key << "\n";     \
            return false;                                                           \
        }                                                                           \
    }                                                                               \
    void NAME(ENUM_TYPE in_##NAME)                                                  \
    {                                                                               \
        m_##NAME = in_##NAME;                                                       \
    }                                                                               \
    ENUM_TYPE NAME() const                                                          \
    {                                                                               \
        return m_##NAME;                                                            \
    }                                                                               \
    int NAME##_as_int() const                                                       \
    {                                                                               \
        return static_cast<int>(m_##NAME);                                          \
    }                                                                               \
 protected:                                                                         \
    ENUM_TYPE m_##NAME;


//time_t
// #ifdef WIN32
// 自动生成 time_t 的宏
// #define PROPERTY_TIME(TYPE, M)                                               \
// public:                                                                      \
//     bool M(const nlohmann::json &json, const std::string &key)               \
// {                                                                        \
//         if (JsonTypeChecker<TYPE>::check(json, key))                         \
//     {                                                                    \
//             m_##M = json[key].get<TYPE>();                                   \
//             return true;                                                     \
//     }                                                                    \
//         else                                                                 \
//     {                                                                    \
//             std::cerr << "Failed to set [" #M "] from key: " << key << "\n"; \
//             return false;                                                    \
//     }                                                                    \
// }                                                                        \
//     void M(const TYPE &in_##M)                                               \
// {                                                                        \
//         m_##M = in_##M;                                                      \
// }                                                                        \
//     TYPE M() const                                                           \
// {                                                                        \
//         return m_##M;                                                        \
// }                                                                        \
//                                                                              \
//     protected:                                                                   \
//     TYPE m_##M;
// #else
#define PROPERTY_TIME(TYPE,M)                                                            \
public:                                                                             \
    bool M(const nlohmann::json &json, const std::string &key)                      \
{                                                                               \
        if (JsonTypeChecker<int64_t>::check(json, key))                            \
    {                                                                           \
            m_##M = static_cast<time_t>(json[key].get<int64_t>());                  \
            return true;                                                            \
    }                                                                           \
        else                                                                        \
    {                                                                           \
            std::cerr << "Failed to set [" #M "] from key: " << key << "\n";       \
            return false;                                                           \
    }                                                                           \
}                                                                               \
    void M(const time_t &in_##M) { m_##M = in_##M; }                               \
    int64_t M() const { return static_cast<int64_t>(m_##M); }                       \
                                                                                    \
    protected:                                                                          \
    time_t m_##M;

// #endif




// 自动生成 Getter/Setter 的宏
#define PROPERTY_AUTO(TYPE, M)                                               \
public:                                                                      \
    bool M(const nlohmann::json &json, const std::string &key)               \
    {                                                                        \
        if (JsonTypeChecker<TYPE>::check(json, key))                         \
        {                                                                    \
            m_##M = json[key].get<TYPE>();                                   \
            return true;                                                     \
        }                                                                    \
        else                                                                 \
        {                                                                    \
            std::cerr << "Failed to set [" #M "] from key: " << key << "\n"; \
            return false;                                                    \
        }                                                                    \
    }                                                                        \
    void M(const TYPE &in_##M)                                               \
    {                                                                        \
        m_##M = in_##M;                                                      \
    }                                                                        \
    TYPE M() const                                                           \
    {                                                                        \
        return m_##M;                                                        \
    }                                                                        \
protected:                                                                   \
    TYPE m_##M;

#define PROPERTY_AUTO_P(TYPE, M) \
public:                          \
    void M(TYPE in_##M)          \
    {                            \
        m_##M = in_##M;          \
    }                            \
    TYPE M() const               \
    {                            \
        return m_##M;            \
    }                            \
protected:                       \
    TYPE m_##M = nullptr;





// C++ 增删改查对象
#define PROPERTY_CONTEXT(M,N)                                       \
private:                                                            \
  std::unordered_map<std::string, std::shared_ptr<N>> m_##N_map;    \
public:                                                             \
int add##M(std::string UID, json info)                              \
{                                                                   \
    auto item = m_##N_map.find(UID);                                \
    if (item != m_##N_map.end())                                    \
    {                                                               \
        return 1;                                                   \
    }                                                               \
    auto obj = std::make_shared<N>();                               \
    obj->UID(UID);                                                  \
    obj->setInfo(info);                                             \
    m_##N_map.insert(std::pair(UID, obj));                          \
    return 0;                                                       \
}                                                                   \
int add##M(std::string UID, std::shared_ptr<N> obj)                 \
{                                                                   \
    auto item = m_##N_map.find(obj->UID());                         \
    if (item != m_##N_map.end())                                    \
    {                                                               \
        return 1;                                                   \
    }                                                               \
    obj->UID(UID);                                                  \
    m_##N_map.insert(std::pair(obj->UID(), obj));                   \
    return 0;                                                       \
}                                                                   \
int del##M(std::string UID)                                         \
{                                                                   \
    auto item = m_##N_map.find(UID);                                \
    if (item == m_##N_map.end())                                    \
    {                                                               \
        return 1;                                                   \
    }                                                               \
    m_##N_map.erase(UID);                                           \
    return 0;                                                       \
}                                                                   \
int set##M(std::string UID, json info)                              \
{                                                                   \
    auto item = m_##N_map.find(UID);                                \
    if (item == m_##N_map.end())                                    \
    {                                                               \
        return 1;                                                   \
    }                                                               \
    item->second->setInfo(info);                                    \
    return 0;                                                       \
}                                                                   \
json get##M(const std::string &UID)                                 \
{                                                                   \
    auto item = m_##N_map.find(UID);                                \
    if (item == m_##N_map.end())                                    \
    {                                                               \
        return json();                                              \
    }                                                               \
    return item->second->info();                                    \
}                                                                   \
std::shared_ptr<N> get##M##Ptr(const std::string &UID)              \
{                                                                   \
    auto it = m_##N_map.find(UID);                                  \
    if (it == m_##N_map.end())                                      \
    {                                                               \
        return nullptr;                                             \
    }                                                               \
    return it->second;                                              \
}                                                                   \
std::unordered_map<std::string, std::shared_ptr<N>> *get##M##Map()  \
{                                                                   \
    return &m_##N_map;                                              \
}


// C++ 执行指令
#define PROPERTY_EXCUTE(M,N,T)                        \
json gen##M##Temp(std::string tempID)               \
{                                                   \
    N task;                                          \
    return task.gen_para(tempID);                    \
}                                                   \
std::string add##M##Task(json info)                 \
{                                                   \
    auto task = std::make_shared<N>();               \
    auto UID = generate_UniqueKey();                \
    info["task_UID"] = UID;                          \
    task->set_para(info);                            \
    m_##T##_map.insert(std::pair(UID,task));        \
    m_##T##_queue.add_task(UID, task);                \
    return UID;                                     \
}                                                   \
int clear##M##Task()                                \
{                                                   \
    return 0;                                       \
}                                                   \



#define PROPERTY_TASK_QUEUE(M,N)                                        \
std::unordered_map<std::string, std::shared_ptr<TaskBase>> m_##N##_map; \
##MQueue m_##N##_queue;                                                 \
int start##M(std::string uid)                                           \
{                                                                       \
    return m_##N##_queue.start_task(uid);                                \
}                                                                       \
int pause##M(std::string uid)                                           \
{                                                                       \
    return m_##N##_queue.pause_task(uid);                                \
}                                                                       \
int unpause##M(std::string uid)                                         \
{                                                                       \
    return m_##N##_queue.unpause_task(uid);                             \
}                                                                       \
int cancel##M(std::string uid)                                          \
{                                                                       \
    return m_##N##_queue.cancel_task(uid);                              \
}                                                                       \
int reset##M(std::string uid)                                           \
{                                                                       \
    return m_##N##_queue.reset_task(uid);                               \
}                                                                       \
int delete##M(std::string uid)                                          \
{                                                                       \
    return m_##N##_queue.delete_task(uid);                              \
}                                                                       \
std::shared_ptr<##MBase> find##M(std::string uid)                       \
{                                                                       \
    return m_##N##_queue.find_task(uid);                                \
}                                                                       \
json get##MStatus(std::string uid)                                      \
{                                                                       \
    auto ##M = m_##N##_queue.find_task(uid);                            \
    if (##M == nullptr)                                                 \
    {                                                                   \
        return json();                                                  \
    }                                                                   \
    json info;                                                          \
    auto state = ##M->get_##N##_info();                                 \
    info["UID"] = uid;                                                  \
    info["state"] = state.state;                                        \
    info["state_info"] = state.state_info;                              \
    info["step"] = state.step;                                          \
    info["step_total"] = state.step_total;                              \
    info["step_info"] = state.step_info;                                \
    info["step_percent"] = state.step_percent;                          \
    info["total_percent"] = state.total_percent;                        \
    info["completed"] = state.completed;                                \
    return info;                                                        \
}                                                                       \
json get##MDetail(std::string uid)                                      \
{                                                                       \
    auto ##M = m_##N##_queue.find_task(uid);                            \
    if (##M == nullptr)                                                 \
    {                                                                   \
        return json();                                                  \
    }                                                                   \
    return ##M->get_detail();                                           \
}                                                                       \
json get##MPara(std::string uid)                                        \
{                                                                       \
    auto ##M = m_##N##_queue.find_task(uid);                            \
    if (##M == nullptr)                                                 \
    {                                                                   \
        return json();                                                  \
    }                                                                   \
    return ##M->get_para();                                             \
}                                                                       \
int set##MPara(std::string uid, json para)                              \
{                                                                       \
    auto ##M = m_##N##_queue.find_task(uid);                            \
    if (##M == nullptr)                                                 \
    {                                                                   \
        return 1;                                                       \
    }                                                                   \
    ##M->set_para(para);                                                \
    return 0;                                                           \
}





 nlohmann::json variantToJson(const QVariant &value);
 nlohmann::json variantMapToJson(const QVariantMap &map);
 nlohmann::json variantListToJson(const QList<QVariantMap> &list);
 nlohmann::json QStringToJson(const QString &str);
 nlohmann::json StringToJson(const std::string &str);

QVariant JsonToQVariant(const nlohmann::json &jsonValue);
QVariantMap JsonToQVariantMap(const nlohmann::json &jsonObj);
QList<QVariant> JsonToQVariantList(const nlohmann::json &jsonArray);
QString JsonToQString(const nlohmann::json &json);




