#pragma once
#include <QObject>
#include <QtQml/qqml.h>
#include <QThreadPool>
#include "CasterMonitor.h"

class CasterResourceController : public QObject
{
    Q_OBJECT

    // 集群负载状态
    Q_PROPERTY_AUTO(double,cluster_cpu)     // 集群负载百分比 （所有节点的CPU均值）
    Q_PROPERTY_AUTO(double,cluster_mem)     // 集群总内存占用
    Q_PROPERTY_AUTO(double,cluster_in)      // 集群入带宽
    Q_PROPERTY_AUTO(double,cluster_out)     // 集群出带宽
    Q_PROPERTY_AUTO(int,cluster_runsec)     // 运行时长

    Q_PROPERTY_AUTO(int,node_online)        // 在线节点数
    Q_PROPERTY_AUTO(int,node_count)         // 总节点数

    Q_PROPERTY_AUTO(int,connect_online)     // 总的连接数量
    Q_PROPERTY_AUTO(int,server_online)      // 在线基站
    Q_PROPERTY_AUTO(int,server_limit)       // 基站上限
    Q_PROPERTY_AUTO(int,client_online)      // 在线基站
    Q_PROPERTY_AUTO(int,client_limit)       // 基站上限


    Q_PROPERTY_AUTO(QList<QVariantMap>, node_status_data)  // Caster节点状态
    QML_SINGLETON
    QML_ELEMENT
private:
    explicit CasterResourceController(QObject *parent = nullptr)
    {

    }

public:
    SINGLETON(CasterResourceController)
    static CasterResourceController *create(QQmlEngine *, QJSEngine *)
    {
        return getInstance();
    }

    // 更新所有信息
    Q_SIGNAL void loadDataStart();
    Q_SIGNAL void loadDataSuccess();
    Q_INVOKABLE void loadData()
    {

    }

    // 更新站点信息
    Q_SIGNAL void updateNodeDataStart();
    Q_SIGNAL void updateNodeDataSuccess();
    Q_INVOKABLE void updateNodeData()
    {
        QThreadPool::globalInstance()->start(
            [this]()
            {
                Q_EMIT updateNodeDataStart();

                m_node_status_data.clear();

                auto data_map=CasterMonitor::getInstance()->m_caster_node_map;

                for(auto iter:data_map)
                {
                    auto info = iter.second->info();
                    QVariantMap data= JsonToQVariantMap(info);

                    if(data["update_flag"].toBool() == false)
                    {
                        // continue;
                    }
                    m_node_status_data.append(data);
                }

                Q_EMIT updateNodeDataSuccess();
            });

    }


private:


private:

};
