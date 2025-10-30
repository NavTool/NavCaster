#pragma once

#include <QObject>
#include <QQmlApplicationEngine>
#include <QTranslator>
#include "stdafx.h"

class AppInfo : public QObject {
    Q_OBJECT
    Q_PROPERTY_AUTO(QString, version)
    Q_PROPERTY_AUTO(QString, locale)
    Q_PROPERTY_READONLY_AUTO(QStringList, locales)
    QML_SINGLETON
    QML_ELEMENT
private:
    explicit AppInfo(QObject *parent = nullptr);
    void initTranslator();

public:
    SINGLETON(AppInfo)
    static AppInfo *create(QQmlEngine *, QJSEngine *) {
        return getInstance();
    }
    void init(QQmlApplicationEngine *);

private:
    QTranslator m_translator;
};
