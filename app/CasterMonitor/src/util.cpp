#include "util.h"



nlohmann::json variantToJson(const QVariant &value)
{
    if (value.type() == QVariant::Map)
    {
        // QVariantMap 转换为 JSON 对象
        QVariantMap map = value.toMap();
        nlohmann::json jsonObj;
        for (auto it = map.begin(); it != map.end(); ++it)
        {
            jsonObj[it.key().toStdString()] = variantToJson(it.value());
        }
        return jsonObj;
    }
    else if (value.type() == QVariant::List)
    {
        // QVariantList 转换为 JSON 数组
        QVariantList list = value.toList();
        nlohmann::json jsonArray = nlohmann::json::array();
        for (const auto &item : list)
        {
            jsonArray.push_back(variantToJson(item));
        }
        return jsonArray;
    }
    else if (value.type() == QVariant::String)
    {
        // QVariantString 转换为 JSON 字符串
        return value.toString().toStdString();
    }
    else if (value.type() == QVariant::Int)
    {
        // QVariantInt 转换为 JSON 整数
        return value.toInt();
    }
    else if (value.type() == QVariant::Double)
    {
        // QVariantDouble 转换为 JSON 浮动
        return value.toDouble();
    }
    else if (value.type() == QVariant::Bool)
    {
        // QVariantBool 转换为 JSON 布尔值
        return value.toBool();
    }
    else
    {
        // 默认返回 null
        return nullptr;
    }
}

nlohmann::json variantMapToJson(const QVariantMap &map)
{
    nlohmann::json jsonObj;
    for (auto it = map.begin(); it != map.end(); ++it)
    {
        jsonObj[it.key().toStdString()] = variantToJson(it.value());
    }
    return jsonObj;
}

nlohmann::json variantListToJson(const QList<QVariantMap> &list)
{
    nlohmann::json jsonArray = nlohmann::json::array();
    for (const QVariantMap &map : list)
    {
        jsonArray.push_back(variantMapToJson(map));
    }
    return jsonArray;
}

nlohmann::json QStringToJson(const QString &str)
{
    return nlohmann::json(str.toStdString()); // 直接将 QString 转换为 JSON 字符串
}


QString JsonToQString(const nlohmann::json &json)
{
    return QString::fromStdString(json.get<std::string>()); // 提取 JSON 字符串并转换为 QString
}

QVariantMap JsonToQVariantMap(const nlohmann::json &jsonObj)
{
    QVariantMap map;
    for (auto it = jsonObj.begin(); it != jsonObj.end(); ++it)
    {
        map.insert(QString::fromStdString(it.key()), JsonToQVariant(it.value()));
    }
    return map;
}


QList<QVariant> JsonToQVariantList(const nlohmann::json &jsonArray)
{
    QList<QVariant> list;
    for (const auto &item : jsonArray)
    {
        list.append(JsonToQVariant(item));
    }
    return list;
}


QVariant JsonToQVariant(const nlohmann::json &jsonValue)
{
    if (jsonValue.is_object())
    {
        // 对象类型，递归转换成 QVariantMap
        return QVariant::fromValue(JsonToQVariantMap(jsonValue));
    }
    else if (jsonValue.is_array())
    {
        // 数组类型，转换为 QList<QVariant>
        return QVariant::fromValue(JsonToQVariantList(jsonValue));
    }
    else if (jsonValue.is_boolean())
    {
        return QVariant(jsonValue.get<bool>());
    }
    else if (jsonValue.is_number_integer())
    {
        return QVariant(jsonValue.get<int>());
    }
    else if (jsonValue.is_number_float())
    {
        return QVariant(jsonValue.get<double>());
    }
    else if (jsonValue.is_string())
    {
        return QVariant(QString::fromStdString(jsonValue.get<std::string>()));
    }
    return QVariant(); // 默认返回空 QVariant
}