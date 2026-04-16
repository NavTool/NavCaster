#pragma once
#include <QObject>
#include <QtQml/qqml.h>
#include <QThreadPool>
#include "CasterMonitor.h"

class AccountDataController : public QObject
{
    Q_OBJECT


    Q_PROPERTY_AUTO(int,account_count)          // 账号计数
    Q_PROPERTY_AUTO(int,expiring_count)         // 即将过期账号数
    Q_PROPERTY_AUTO(int,expired_count)          // 过期账号计数


    Q_PROPERTY_AUTO(QList<QVariantMap>, data)
    QML_ELEMENT
public:
    explicit AccountDataController(QObject *parent = nullptr) : QObject{parent}
    {
    }
    Q_SIGNAL void loadDataStart();
    Q_SIGNAL void loadDataSuccess();
    Q_INVOKABLE void loadData()
    {
        Q_EMIT loadDataStart();

        m_data.clear();

        CasterMonitor::getInstance()->AccountRecords.forEach(
            [this](const std::string &key, const std::shared_ptr<AccountRecord> &obj) {
                m_data.append(PrototoQml(*obj));
            });

        Q_EMIT loadDataSuccess();

    }

private:

};
