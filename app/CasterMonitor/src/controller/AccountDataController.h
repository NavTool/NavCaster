
#pragma once

#include <QObject>
#include <QtQml/qqml.h>
#include <QRandomGenerator>
#include "stdafx.h"

class AccountDataController : public QObject
{
    Q_OBJECT
    Q_PROPERTY_AUTO(QList<QVariantMap>, data)
    QML_ELEMENT
public:
    explicit AccountDataController(QObject *parent = nullptr);
    Q_SIGNAL void loadDataStart();
    Q_SIGNAL void loadDataSuccess();
    Q_INVOKABLE void loadData(const QString UID);

private:

};
