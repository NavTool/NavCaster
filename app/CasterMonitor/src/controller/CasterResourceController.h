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
    Q_PROPERTY_AUTO(double,cluster_recv_total)      // 集群入带宽
    Q_PROPERTY_AUTO(double,cluster_recv_speed)      // 集群入带宽
    Q_PROPERTY_AUTO(double,cluster_send_total)     // 集群出带宽
    Q_PROPERTY_AUTO(double,cluster_send_speed)     // 集群出带宽
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

                m_cluster_cpu=0;
                m_cluster_mem=0;
                m_cluster_recv_total=0;
                m_cluster_recv_speed=0;
                m_cluster_send_total=0;
                m_cluster_send_speed=0;
                m_cluster_runsec=0;
                m_node_online=0;
                m_node_count=0;
                m_connect_online=0;
                m_server_online=0;
                m_server_limit=0;
                m_client_online=0;
                m_client_limit=0;

                m_node_status_data.clear();

                auto data_map=CasterMonitor::getInstance()->m_caster_node_map;

                for(auto iter:data_map)
                {
                    auto info = iter.second->info();
                    QVariantMap data= JsonToQVariantMap(info);

                    m_node_count++;

                    if(data["update_flag"].toBool() == false)
                    {
                        // continue;
                    }

                    m_cluster_cpu += iter.second->cpu_usage();
                    m_cluster_mem += iter.second->mem_usage();
                    m_cluster_recv_total+=iter.second->recv_total();
                    m_cluster_recv_speed+=iter.second->recv_speed();
                    m_cluster_send_total+=iter.second->send_total();
                    m_cluster_send_speed+= iter.second->send_speed();
                    m_connect_online+=iter.second->connnect_count();
                    m_server_online+=iter.second->server_count();
                    m_client_online+=iter.second->client_count();


                    if(iter.second->online_time()!=0)
                    {
                        if(m_cluster_runsec==0)
                        {
                            m_cluster_runsec=iter.second->online_time(); //根据最长节点作为运行时长
                        }
                        else if(m_cluster_runsec>iter.second->online_time())
                        {
                            m_cluster_runsec=iter.second->online_time();
                        }
                    }

                    m_node_online++;

                    m_node_status_data.append(data);
                }

                m_cluster_cpu/=m_node_online*1.0;

                Q_EMIT updateNodeDataSuccess();
            });

    }


private:


private:

};
