#pragma once
#include <QObject>
#include <QtQml/qqml.h>
#include <QThreadPool>
#include "CasterMonitor.h"

class EventDataController : public QObject
{
    Q_OBJECT
    Q_PROPERTY_AUTO(QList<QVariantMap>, data)
    QML_ELEMENT
public:
    explicit EventDataController(QObject *parent = nullptr) : QObject{parent}
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


                for(int i=100;i<120;i++)
                {


                    QVariantMap data;//= JsonToQVariantMap(info);

                    data["mpt"]=i;

                    m_data.append(data);
                }


                Q_EMIT loadDataSuccess();
            });
    }

private:

};
