#pragma once
#include <QObject>
#include <QtQml/qqml.h>
#include <QThreadPool>
#include "CasterMonitor.h"

class PushDataController : public QObject
{
    Q_OBJECT
    Q_PROPERTY_AUTO(QList<QVariantMap>, data)
    QML_ELEMENT
public:
    explicit PushDataController(QObject *parent = nullptr) : QObject{parent}
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

                m_data.clear();  //清除数据


                auto data_map=CasterMonitor::getInstance()->m_relay_push_map;

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

private:

};
