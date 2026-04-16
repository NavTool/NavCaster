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

                QList<QVariantMap> result;

                CasterMonitor::getInstance()->ClientStates.forEach(
                    [&result](const std::string &key, const std::shared_ptr<ClientState> &obj) {
                        result.append(PrototoQml(*obj));
                    });

                QMetaObject::invokeMethod(this, [this, result = std::move(result)]() {
                    data(result);
                    Q_EMIT loadDataSuccess();
                });
            });

    }
private slots:

};




