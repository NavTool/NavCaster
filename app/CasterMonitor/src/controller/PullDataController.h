#pragma once
#include <QObject>
#include <QtQml/qqml.h>
#include <QThreadPool>
#include "CasterMonitor.h"

class PullDataController : public QObject
{
    Q_OBJECT
    Q_PROPERTY_AUTO(QList<QVariantMap>, data)
    QML_ELEMENT
public:
    explicit PullDataController(QObject *parent = nullptr) : QObject{parent}
    {
    }
    Q_SIGNAL void loadDataStart();
    Q_SIGNAL void loadDataSuccess();
    Q_INVOKABLE void loadData()
    {
        //创建一个线程执行数据读取操作
        QThreadPool::globalInstance()->start(
            [this]()
            {
                Q_EMIT loadDataStart();

                m_data.clear();

                auto data_map = CasterMonitor::getInstance()->m_relay_pull_items.getSnapshot();
                auto stat_map = CasterMonitor::getInstance()->m_relay_pull_stats.getSnapshot();

                for(auto &[key, obj] : data_map)
                {
                    QVariantMap data = JsonToQVariantMap(obj->info());

                    auto stat_item = stat_map.find(key);
                    if(stat_item != stat_map.end())
                    {
                        data["connect_key"] = stat_item->second->connect_key().c_str();
                        data["state"] = stat_item->second->state();
                    }

                    m_data.append(data);
                }

                Q_EMIT loadDataSuccess();
            });
    }

private:

};
