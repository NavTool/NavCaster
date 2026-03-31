#pragma once

#include <functional>
#include <mutex>
#include <unordered_map>
#include <QString>
#include <QVariantMap>
#include "util.h"

template <typename T>
class Context
{
    std::unordered_map<QString, std::shared_ptr<T>> m_obj_map;
    std::recursive_mutex m_map_lock;

public:
    // 从 Redis HGETALL 结果全量同步本地 map
    // 标记所有现存对象 update_flag=false → 遍历新数据 upsert → 删除未更新的对象
    void syncFromRedis(const QVariantMap &redis_data)
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);

        // 1) 标记所有现有对象为"未更新"
        for (auto &[key, obj] : m_obj_map)
            obj->update_flag(false);

        // 2) 遍历 Redis 返回的 key-value，upsert 到本地 map
        for (auto it = redis_data.begin(); it != redis_data.end(); ++it)
        {
            QString key = it.key();
            auto parsed_json = QStringToJson(it.value().toString());

            auto item = m_obj_map.find(key);
            if (item == m_obj_map.end())
            {
                auto obj = std::make_shared<T>();
                m_obj_map.insert(std::pair(key, obj));
                item = m_obj_map.find(key);
            }
            item->second->setInfo(parsed_json);
            item->second->update_flag(true);
        }

        // 3) 清除本次未更新的元素
        for (auto it = m_obj_map.begin(); it != m_obj_map.end();)
        {
            if (!it->second->update_flag())
                it = m_obj_map.erase(it);
            else
                ++it;
        }
    }

    int addObject(const QString &UID, std::shared_ptr<T> obj)
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        if (m_obj_map.find(UID) != m_obj_map.end())
            return 1;
        m_obj_map.insert(std::pair(UID, obj));
        return 0;
    }

    int delObject(const QString &UID)
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        if (m_obj_map.find(UID) == m_obj_map.end())
            return 1;
        m_obj_map.erase(UID);
        return 0;
    }

    std::shared_ptr<T> getObjectPtr(const QString &UID)
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        auto it = m_obj_map.find(UID);
        if (it == m_obj_map.end())
            return nullptr;
        return it->second;
    }

    void forEach(const std::function<void(const QString &, const std::shared_ptr<T> &)> &callback)
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        for (const auto &[key, obj] : m_obj_map)
            callback(key, obj);
    }

    // 返回内部 map 的线程安全副本（适合在线程池中使用）
    std::unordered_map<QString, std::shared_ptr<T>> getSnapshot()
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        return m_obj_map;
    }

    size_t size()
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        return m_obj_map.size();
    }
};
