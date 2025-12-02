#pragma once
#include <QObject>
#include <QtQml/qqml.h>
#include <QThreadPool>
#include "CasterMonitor.h"

class ClientDataController : public QObject
{
    Q_OBJECT
    Q_PROPERTY_AUTO(QList<QVariantMap>, data)
    QML_ELEMENT
public:
    explicit ClientDataController(QObject *parent = nullptr) : QObject{parent}
    {

    }
    Q_SIGNAL void loadDataStart();
    Q_SIGNAL void loadDataSuccess();
    Q_INVOKABLE void loadData()
    {
        QThreadPool::globalInstance()->start(
            [this]()
            {
                Q_EMIT loadDataStart();

                m_data.clear();

                auto data_map=CasterMonitor::getInstance()->m_ntrip_client_map;

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
            });

    }
private slots:

};




