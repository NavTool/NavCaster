#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QVariantMap>

#include "util.h"
#include "network/HttpClient.h"


// 操作类型枚举（与原 HashOperate.h 兼容）
enum class HashOperateType
{
    UNKNOWN = 0,
    ADD = 1,
    DEL = 2,
    SET = 3,
    GET = 4,
    GET_ALL = 5
};


// ============================================================================
//  HttpHashContext —— 通过 HTTP API 进行 CRUD 操作的本地上下文管理
//
//  替换原 HashContext<T, Table>，接口完全兼容，底层从 Redis 改为 HTTP API。
//  T       : protobuf 消息类型
// ============================================================================
template <typename T>
class HttpHashContext
{
public:
    using HashOperateFinishedHandler = std::function<void(HashOperateType, QString, bool, QVariantMap)>;

private:
    HttpClient *m_client = nullptr;
    std::string m_api_path;  // e.g. "/api/accounts"
    std::unordered_map<std::string, std::shared_ptr<T>> m_obj_map;
    std::recursive_mutex m_map_lock;
    HashOperateFinishedHandler m_hash_operate_finished_handler;

public:
    // ==================== 绑定 ====================

    void setClient(HttpClient *client) { m_client = client; }
    HttpClient *client() const { return m_client; }

    void setApiPath(const std::string &path) { m_api_path = path; }
    const std::string &apiPath() const { return m_api_path; }

    // ==================== QML 友好的 CRUD 接口 ====================

    QVariantMap generateTemplate()
    {
        return PrototoQml(T());
    }

    QVariantMap getItemInfo(const QString &field)
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        auto it = m_obj_map.find(field.toStdString());
        if (it == m_obj_map.end())
            return PrototoQml(T());
        return PrototoQml(*it->second);
    }

    QVariantMap getAllItemInfo()
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        QVariantMap result;
        for (const auto &[key, obj] : m_obj_map)
        {
            result[QString::fromStdString(key)] =
                QString::fromStdString(ProtoToJson(*obj));
        }
        return result;
    }

    QString addItem(const QString &field, const QVariantMap &info)
    {
        std::string json_str = variantMapToJsonStr(info).toStdString();
        return submitAdd(field.toStdString(), json_str);
    }

    QString delItem(const QString &field)
    {
        return submitDel(field.toStdString());
    }

    QString setItem(const QString &field, const QVariantMap &info)
    {
        std::string json_str = variantMapToJsonStr(info).toStdString();
        return submitSet(field.toStdString(), json_str);
    }

    QString fetchItem(const QString &field)
    {
        return submitGet(field.toStdString());
    }

    QString refreshAll()
    {
        return submitGetAll();
    }

    // ==================== 本地缓存查询 ====================

    std::string getObjectInfo(const std::string &key)
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        auto it = m_obj_map.find(key);
        if (it == m_obj_map.end())
            return std::string();
        return ProtoToJson(*it->second);
    }

    std::shared_ptr<T> getLocalObject(const std::string &key)
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        auto it = m_obj_map.find(key);
        return (it != m_obj_map.end()) ? it->second : nullptr;
    }

    bool contains(const std::string &key)
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        return m_obj_map.find(key) != m_obj_map.end();
    }

    size_t size()
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        return m_obj_map.size();
    }

    void forEach(const std::function<void(const std::string &, const std::shared_ptr<T> &)> &callback)
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        for (const auto &[key, obj] : m_obj_map)
        {
            callback(key, obj);
        }
    }

    std::unordered_map<std::string, std::shared_ptr<T>> *getObjs()
    {
        return &m_obj_map;
    }

    void clear()
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        m_obj_map.clear();
    }

    // ==================== 回调注册 ====================

    void setNoticeHashOperateFinishedHandler(HashOperateFinishedHandler handler)
    {
        m_hash_operate_finished_handler = std::move(handler);
    }

private:
    QString generateOpUid()
    {
        return QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    // ---- ADD: POST /api/{resource} ----
    QString submitAdd(const std::string &field, const std::string &json_str)
    {
        if (!m_client) return {};

        QString op_uid = generateOpUid();
        QString path = QString::fromStdString(m_api_path);

        m_client->post(path, QByteArray::fromStdString(json_str),
            [this, field, json_str, op_uid](bool success, int statusCode, const QByteArray &) {
                bool ok = success && (statusCode == 200 || statusCode == 201);
                if (ok)
                {
                    std::lock_guard<std::recursive_mutex> lock(m_map_lock);
                    auto obj = std::make_shared<T>();
                    if (JsonToProto(json_str, *obj))
                    {
                        m_obj_map[field] = obj;
                    }
                }
                notifyHandler(HashOperateType::ADD, op_uid, ok, QVariantMap());
            });

        return op_uid;
    }

    // ---- DEL: DELETE /api/{resource}/{field} ----
    QString submitDel(const std::string &field)
    {
        if (!m_client) return {};

        QString op_uid = generateOpUid();
        QString path = QString::fromStdString(m_api_path) + "/" +
                        QString::fromStdString(field);

        m_client->del(path,
            [this, field, op_uid](bool success, int statusCode, const QByteArray &) {
                bool ok = success && statusCode == 200;
                if (ok)
                {
                    std::lock_guard<std::recursive_mutex> lock(m_map_lock);
                    m_obj_map.erase(field);
                }
                notifyHandler(HashOperateType::DEL, op_uid, ok, QVariantMap());
            });

        return op_uid;
    }

    // ---- SET: PUT /api/{resource}/{field} ----
    QString submitSet(const std::string &field, const std::string &json_str)
    {
        if (!m_client) return {};

        QString op_uid = generateOpUid();
        QString path = QString::fromStdString(m_api_path) + "/" +
                        QString::fromStdString(field);

        m_client->put(path, QByteArray::fromStdString(json_str),
            [this, field, json_str, op_uid](bool success, int statusCode, const QByteArray &) {
                bool ok = success && statusCode == 200;
                if (ok)
                {
                    std::lock_guard<std::recursive_mutex> lock(m_map_lock);
                    auto obj = std::make_shared<T>();
                    if (JsonToProto(json_str, *obj))
                    {
                        m_obj_map[field] = obj;
                    }
                }
                notifyHandler(HashOperateType::SET, op_uid, ok, QVariantMap());
            });

        return op_uid;
    }

    // ---- GET: GET /api/{resource}/{field} ----
    QString submitGet(const std::string &field)
    {
        if (!m_client) return {};

        QString op_uid = generateOpUid();
        QString path = QString::fromStdString(m_api_path) + "/" +
                        QString::fromStdString(field);

        m_client->get(path,
            [this, field, op_uid](bool success, int statusCode, const QByteArray &body) {
                QVariantMap info;
                bool ok = success && statusCode == 200;
                if (ok)
                {
                    // 响应 body 是该字段对应的 JSON 对象
                    std::string value_json = body.toStdString();
                    {
                        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
                        auto obj = std::make_shared<T>();
                        if (JsonToProto(value_json, *obj))
                        {
                            m_obj_map[field] = obj;
                        }
                    }
                    info[QString::fromStdString(field)] = QString::fromStdString(value_json);
                }
                notifyHandler(HashOperateType::GET, op_uid, ok, info);
            });

        return op_uid;
    }

    // ---- GET_ALL: GET /api/{resource} ----
    QString submitGetAll()
    {
        if (!m_client) return {};

        QString op_uid = generateOpUid();
        QString path = QString::fromStdString(m_api_path);

        m_client->get(path,
            [this, op_uid](bool success, int statusCode, const QByteArray &body) {
                QVariantMap info;
                bool ok = success && statusCode == 200;
                if (ok)
                {
                    QJsonDocument doc = QJsonDocument::fromJson(body);
                    if (doc.isObject())
                    {
                        QJsonObject root = doc.object();
                        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
                        m_obj_map.clear();

                        for (auto it = root.begin(); it != root.end(); ++it)
                        {
                            // 将每个值重新序列化为 JSON 字符串
                            QString value_json;
                            if (it.value().isObject())
                            {
                                value_json = QString::fromUtf8(
                                    QJsonDocument(it.value().toObject()).toJson(QJsonDocument::Compact));
                            }
                            else if (it.value().isString())
                            {
                                // 值本身就是字符串（兼容非 JSON 值）
                                value_json = it.value().toString();
                            }
                            else
                            {
                                continue;
                            }

                            auto obj = std::make_shared<T>();
                            if (JsonToProto(value_json.toStdString(), *obj))
                            {
                                m_obj_map[it.key().toStdString()] = obj;
                            }
                            info[it.key()] = value_json;
                        }
                    }
                }
                notifyHandler(HashOperateType::GET_ALL, op_uid, ok, info);
            });

        return op_uid;
    }

    void notifyHandler(HashOperateType type, const QString &op_uid, bool success, const QVariantMap &info)
    {
        if (m_hash_operate_finished_handler)
        {
            m_hash_operate_finished_handler(type, op_uid, success, info);
        }
    }
};
