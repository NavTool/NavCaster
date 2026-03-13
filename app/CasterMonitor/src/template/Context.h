#pragma once

#include <unordered_map>

template <typename T>
class Context
{
    std::unordered_map<std::string, std::shared_ptr<T>> m_obj_map;

    std::recursive_mutex m_map_lock;

public:
    int addObject(const std::string &UID, std::string json_str)
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);

        // 创建一个对象
        auto obj = std::make_shared<T>();
        if (!JsonToProto(json_str, *obj)) // 解析对象
        {
            return 1; // 解析失败
        }
        return addObject(UID, obj);
    }

    int addObject(const std::string &UID, std::shared_ptr<T> obj)
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        auto item = m_obj_map.find(UID);
        if (item != m_obj_map.end())
        {
            return 1;
        }
        // obj->set_uid(UID);
        m_obj_map.insert(std::pair(UID, obj));
        return 0;
    }

    int delObject(const std::string &UID)
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        auto item = m_obj_map.find(UID);
        if (item == m_obj_map.end())
        {
            return 1; // 在map中不存在
        }
        m_obj_map.erase(UID);
        return 0;
    }

    int setObject(const std::string &UID, std::string json_str)
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        auto item = m_obj_map.find(UID);
        if (item == m_obj_map.end())
        {
            return 1;
        }
        if (!JsonToProto(json_str, *item->second)) // 解析对象
        {
            return 2; // 解析失败
        }
        return 0;
    }

    std::string getObject(const std::string &UID)
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        auto item = m_obj_map.find(UID);
        if (item == m_obj_map.end())
        {
            return std::string(); // 找不到
        }
        return ProtoToJson(*item->second);
    }

    std::shared_ptr<T> getObjectPtr(const std::string &UID)
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        auto it = m_obj_map.find(UID);
        if (it == m_obj_map.end())
        {
            return nullptr;
        }
        return it->second;
    }

    void forEach(const std::function<void(const std::string &, const std::shared_ptr<T> &)> &callback)
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        for (const auto &[key, st] : m_obj_map)
        {
            callback(key, st);
        }
    }

    std::unordered_map<std::string, std::shared_ptr<T>> *getObjs()
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        return &m_obj_map;
    }

    void setUnpdateFlagFalse()
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        for (auto iter : m_obj_map)
        {
            iter.second->set_update_flag(false);
        }
    }

    void clearUnpdateFlagFalse()
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        auto it = m_obj_map.begin();
        while (it != m_obj_map.end())
        {
            if (it->second->update_flag() == false)
            {
                it = m_obj_map.erase(it); // 删除元素，并更新迭代器
            }
            else
            {
                ++it; // 仅在未删除时前进迭代器
            }
        }
    }
};