#pragma once
#include <QObject>
#include <QtQml/qqml.h>
#include "stdafx.h"
#include "spdlog/spdlog.h"


class CasterMonitor : public QObject
{
    Q_OBJECT

    Q_PROPERTY_AUTO(QVariantMap, project_info) // 站点信息

    QML_SINGLETON
    QML_ELEMENT
private:
    explicit CasterMonitor(QObject *parent = nullptr) : QObject(parent) { m_logger = spdlog::default_logger(); }

public:
    SINGLETON(CasterMonitor)
    static CasterMonitor *create(QQmlEngine *, QJSEngine *)
    {
        return getInstance();
    }
public:


     Q_INVOKABLE bool init_Connect(QVariantMap connect_info);





public:
    std::shared_ptr<spdlog::logger> m_logger; // 模块日志器

};

