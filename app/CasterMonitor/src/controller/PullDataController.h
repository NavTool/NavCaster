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

                auto data_map=CasterMonitor::getInstance()->m_relay_pull_list_map;   // 先从list中拉取所有的目标任务
                auto stat_map=CasterMonitor::getInstance()->m_relay_pull_stat_map;   // 再从Stat中拉取当前已经在执行的任务的状态

                for(auto iter:data_map)
                {
                    auto info = iter.second->info();
                    QVariantMap data= JsonToQVariantMap(info);


                    auto stat_item=stat_map.find(iter.first);
                    if(stat_item!=stat_map.end())
                    {
                        data["connect_key"]=stat_item->second->connect_key().c_str();
                        data["state"]=stat_item->second->state();
                    }

                    if(data["update_flag"].toBool() == false)
                    {
                        // continue;
                    }
                    m_data.append(data);
                }

                Q_EMIT loadDataSuccess();
            });
    }

private:

};
