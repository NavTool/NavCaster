#pragma once
#include <QObject>
#include <QtQml/qqml.h>
#include <QThreadPool>
#include "CasterMonitor.h"

class CasterResourceController : public QObject
{
    Q_OBJECT

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
