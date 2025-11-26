#pragma once

#include <QObject>
#include <QtQml/qqml.h>
#include <QRandomGenerator>
#include "stdafx.h"

class CasterResourceController : public QObject
{
    Q_OBJECT

    Q_PROPERTY_AUTO(QList<QVariantMap>, node_status_data)  // Caster节点状态



    QML_SINGLETON
    QML_ELEMENT
private:
    explicit CasterResourceController(QObject *parent = nullptr);

public:
    SINGLETON(CasterResourceController)
    static CasterResourceController *create(QQmlEngine *, QJSEngine *)
    {
        return getInstance();
    }

    // 更新所有信息
    Q_SIGNAL void loadDataStart();
    Q_SIGNAL void loadDataSuccess();
    Q_INVOKABLE void loadData();

    // 更新站点信息
    Q_SIGNAL void updateNodeDataStart();
    Q_SIGNAL void updateNodeDataSuccess();
    Q_INVOKABLE void updateNodeData();


private:


private:

};
