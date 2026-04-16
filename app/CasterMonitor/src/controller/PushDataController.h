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

                QList<QVariantMap> result;

                CasterMonitor::getInstance()->PushRecords.forEach(
                    [&result](const std::string &key, const std::shared_ptr<PushRecord> &obj) {
                        QVariantMap data = PrototoQml(*obj);

                        auto stat = CasterMonitor::getInstance()->PushStates.getLocalObject(key);
                        if (stat)
                        {
                            auto stat_data = PrototoQml(*stat);
                            for (auto it = stat_data.begin(); it != stat_data.end(); ++it)
                            {
                                if (it.key() != "uid")
                                    data[it.key()] = it.value();
                            }
                        }

                        result.append(data);
                    });

                QMetaObject::invokeMethod(this, [this, result = std::move(result)]() {
                    data(result);
                    Q_EMIT loadDataSuccess();
                });
            });
    }

private:

};
