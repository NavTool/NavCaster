#pragma once

#include <QtCore/qobject.h>
#include <QtQml/qqml.h>
#include <QSettings>
#include <QScopedPointer>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDir>
#include "stdafx.h"

class SettingsHelper : public QObject
{
    Q_OBJECT
    QML_SINGLETON
    QML_ELEMENT
private:
    explicit SettingsHelper(QObject *parent = nullptr);

public:
    SINGLETON(SettingsHelper)
    static SettingsHelper *create(QQmlEngine *, QJSEngine *)
    {
        return getInstance();
    }
    ~SettingsHelper() override;
    void init(char *argv[]);
    Q_INVOKABLE void saveDarkMode(int darkModel)
    {
        save("darkMode", darkModel);
    }
    Q_INVOKABLE int getDarkMode()
    {
        return get("darkMode", QVariant(0)).toInt();
    }
    Q_INVOKABLE void saveUseSystemAppBar(bool useSystemAppBar)
    {
        save("useSystemAppBar", useSystemAppBar);
    }
    Q_INVOKABLE bool getUseSystemAppBar()
    {
        return get("useSystemAppBar", QVariant(false)).toBool();
    }
    Q_INVOKABLE bool getSystemScalingAdjusted() {
        return get("SystemScalingAdjusted", QVariant(true)).toBool();
    }
    Q_INVOKABLE void saveSystemScalingAdjusted(const QString &val) {
        save("SystemScalingAdjusted", val);
    }
    Q_INVOKABLE void saveAppKey(const QString &appkey) {
        save("appkey", appkey);
    }
    Q_INVOKABLE QString getAppKey() {
        return get("appkey", QVariant("")).toString();
    }
    Q_INVOKABLE void saveLocale(const QString &locale)
    {
        save("locale", locale);
    }
    Q_INVOKABLE QString getLocale()
    {
        return get("locale", QVariant("zh_CN")).toString();
    }

private:
    void save(const QString &key, QVariant val);
    QVariant get(const QString &key, QVariant def = {});

private:
    QScopedPointer<QSettings> m_settings;
};
