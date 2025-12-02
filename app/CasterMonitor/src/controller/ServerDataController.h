#pragma once
#include <QObject>
#include <QtQml/qqml.h>
#include <QThreadPool>
#include "CasterMonitor.h"


class ServerDataController : public QObject
{
    Q_OBJECT
    Q_PROPERTY_AUTO(QList<QVariantMap>, data)
    QML_ELEMENT
public:
    explicit ServerDataController(QObject *parent = nullptr) : QObject{parent}
    {
        //连接信号和槽
        // connect(_redis_op.get(),&EventUpdateServerData::updateDataFinished,this,&ServerDataController::updateData);
    }
    Q_SIGNAL void loadDataStart();
    Q_SIGNAL void loadDataSuccess();
    Q_INVOKABLE void loadData()
    {
        Q_EMIT loadDataStart();

        m_data.clear();

        auto data_map=CasterMonitor::getInstance()->m_ntrip_server_map;

        for(auto iter:data_map)
        {
            auto info = iter.second->info();
            QVariantMap data= JsonToQVariantMap(info);

            if(data["update_flag"].toBool() == false)
            {
                // continue;
            }
            m_data.append(data);
        }

        Q_EMIT loadDataSuccess();

    }
private slots:

};




