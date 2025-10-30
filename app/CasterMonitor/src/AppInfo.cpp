#include "AppInfo.h"

#include <QQmlContext>
#include <QGuiApplication>
#include <QQmlApplicationEngine>

AppInfo::AppInfo(QObject *parent) : QObject{parent} {
    version(PROJECT_VERSION);
    m_locales << "en_US";
    m_locales << "zh_CN";
    m_locale = QLocale::system().name();
}

void AppInfo::init(QQmlApplicationEngine *engine) {
    initTranslator();
    if ("ON" == QString(PROJECT_HOTLOAD_ENABLED)) {
        engine->setBaseUrl(QUrl(QString("file:///%1/").arg(PROJECT_SOURCE_DIR)));
    } else {
        engine->setBaseUrl(QUrl("qrc:/qt/qml/CasterMonitor/"));
    }
}

void AppInfo::initTranslator() {
    if (m_translator.load(
            QString::fromStdString("%1/%2_%3.qm").arg(":/qt/qml/CasterMonitor", "CasterMonitor", m_locale))) {
        QGuiApplication::installTranslator(&m_translator);
    }
    QLocale::setDefault(QLocale(this->m_locale));
}
