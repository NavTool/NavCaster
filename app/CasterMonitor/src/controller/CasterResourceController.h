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

                double l_cluster_cpu=0;
                double l_cluster_mem=0;
                double l_cluster_recv_total=0;
                double l_cluster_recv_speed=0;
                double l_cluster_send_total=0;
                double l_cluster_send_speed=0;
                int l_cluster_runsec=0;
                int l_node_online=0;
                int l_node_count=0;
                int l_connect_online=0;
                int l_server_online=0;
                int l_server_limit=0;
                int l_client_online=0;
                int l_client_limit=0;

                QList<QVariantMap> l_node_status_data;

                CasterMonitor::getInstance()->CasterNodes.forEach(
                    [&](const std::string &key, const std::shared_ptr<CasterNode> &obj)
                    {
                        QVariantMap data = PrototoQml(*obj);

                        l_node_count++;

                        l_cluster_cpu += obj->cpu_usage();
                        l_cluster_mem += obj->mem_usage();
                        l_cluster_recv_total+=obj->recv_total();
                        l_cluster_recv_speed+=obj->recv_speed();
                        l_cluster_send_total+=obj->send_total();
                        l_cluster_send_speed+= obj->send_speed();
                        l_connect_online+=obj->connect_count();
                        l_server_online+=obj->server_count();
                        l_client_online+=obj->client_count();


                        if(obj->online_time()!=0)
                        {
                            if(l_cluster_runsec==0)
                            {
                                l_cluster_runsec=obj->online_time();
                            }
                            else if(l_cluster_runsec>obj->online_time())
                            {
                                l_cluster_runsec=obj->online_time();
                            }
                        }

                        l_node_online++;

                        l_node_status_data.append(data);
                    });

                if(l_node_online > 0)
                    l_cluster_cpu/=l_node_online*1.0;

                QMetaObject::invokeMethod(this, [this,
                    l_cluster_cpu, l_cluster_mem,
                    l_cluster_recv_total, l_cluster_recv_speed,
                    l_cluster_send_total, l_cluster_send_speed,
                    l_cluster_runsec,
                    l_node_online, l_node_count,
                    l_connect_online, l_server_online, l_server_limit,
                    l_client_online, l_client_limit,
                    l_node_status_data = std::move(l_node_status_data)]() {
                    cluster_cpu(l_cluster_cpu);
                    cluster_mem(l_cluster_mem);
                    cluster_recv_total(l_cluster_recv_total);
                    cluster_recv_speed(l_cluster_recv_speed);
                    cluster_send_total(l_cluster_send_total);
                    cluster_send_speed(l_cluster_send_speed);
                    cluster_runsec(l_cluster_runsec);
                    node_online(l_node_online);
                    node_count(l_node_count);
                    connect_online(l_connect_online);
                    server_online(l_server_online);
                    server_limit(l_server_limit);
                    client_online(l_client_online);
                    client_limit(l_client_limit);
                    node_status_data(l_node_status_data);
                    Q_EMIT updateNodeDataSuccess();
                });
            });

    }


private:


private:

};
