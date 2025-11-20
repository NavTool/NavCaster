
#pragma once

#include <QObject>
#include <QtQml/qqml.h>
#include <QRandomGenerator>
#include "EventOperationBase.h"
#include "stdafx.h"
#include "util.h"

class EventUpdateClientData : public RedisOperationBase
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit EventUpdateClientData(QObject *parent = nullptr): RedisOperationBase(parent) {};

    Q_INVOKABLE QString name() const override { return typeid(this).name(); }

    void execute(redisAsyncContext *ctx) override;

    static void Redis_Update_Data_Callback(redisAsyncContext *c, void *r, void *privdata);;

    Q_SIGNAL void updateDataFinished();

public:
    QList<QVariantMap>  m_data;

};


class ClientDataController : public QObject
{
    Q_OBJECT
    Q_PROPERTY_AUTO(QList<QVariantMap>, data)
    QML_ELEMENT
public:
    explicit ClientDataController(QObject *parent = nullptr);
    Q_SIGNAL void loadDataStart();
    Q_SIGNAL void loadDataSuccess();
    Q_INVOKABLE void loadData();
private slots:
    void updateData();

private:

    std::shared_ptr<EventUpdateClientData> _redis_op = std::make_shared<EventUpdateClientData>();

};




