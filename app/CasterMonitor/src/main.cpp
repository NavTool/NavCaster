#include "AppInfo.h"
#include "Log.h"
#include "SettingsHelper.h"
#include <QGuiApplication>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QtQml/qqmlextensionplugin.h>
#include "version.h"
#include "frame.hpp"


#ifdef FLUENTUI_BUILD_STATIC_LIB
Q_IMPORT_QML_PLUGIN(FluentUIPlugin)
#endif



#define EXE_APPLICATION_NAME "CasterMonitor"                       // 应用程序名
#define EXE_APPLICATION_DISPLAY_NAME "CasterMonitor"               // 应用程序显示名
#define EXE_ORGANIZATION_NAME "NavTool"                      // 组织名称
#define EXE_ORGANIZATION_DOMAIN "https://github.com/NavTool" // 组织域名
#define EXE_APPLICATION_CDESCRIPTION "A Navigation Tool"




int main(int argc, char *argv[]) {
    qputenv("QT_QUICK_CONTROLS_STYLE", "FluentUI");
    QGuiApplication::setApplicationName(EXE_APPLICATION_NAME);
    QGuiApplication::setApplicationDisplayName(EXE_APPLICATION_DISPLAY_NAME);
    QGuiApplication::setOrganizationName(EXE_ORGANIZATION_NAME);
    QGuiApplication::setOrganizationDomain(EXE_ORGANIZATION_DOMAIN);

    // 初始化日志，传入可执行程序路径和应用程序名称
    Log::setup(argv, EXE_APPLICATION_DISPLAY_NAME);
    // 初始化保存设置和读取设置的实例
    SettingsHelper::getInstance()->init(argv);

    if(!SettingsHelper::getInstance()->getSystemScalingAdjusted()){
        qputenv("QT_FONT_DPI", "96");
    }else{
        qputenv("QT_FONT_DPI", "");
    }



    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    Register_qml_frame_define(engine.rootContext()); // 注册宏定义、枚举类型到qml中

    engine.addImportPath(":/qt/qml");
    AppInfo::getInstance()->init(&engine);
    const QUrl url(u"qrc:/qt/qml/CasterMonitor/qml/App.qml"_qs);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated, &app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);
    engine.load(url);
    const int exec = QGuiApplication::exec();
    return exec;
}
