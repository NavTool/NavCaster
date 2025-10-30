#pragma once
#include <QQmlEngine>
#include <qflags.h>
#include <qqmlcontext.h>
#include "Version.h"



#include <QObject>
#include <QtQml>

//// 这些是用来设置程序的基本信息的（QGuiApplication::setxxxxx）（不暴露给界面）
// // 图形化程序相关信息
#define EXE_APPLICATION_NAME "CasterMonitor"                       // 应用程序名
#define EXE_APPLICATION_DISPLAY_NAME "CasterMonitor"               // 应用程序显示名
#define EXE_APPLICATION_VERSION "1.0.2.21"                   // 应用程序版本
#define EXE_ORGANIZATION_NAME "NavTool"                      // 组织名称
#define EXE_ORGANIZATION_DOMAIN "https://github.com/NavTool" // 组织域名
#define EXE_APPLICATION_CDESCRIPTION "A Navigation Tool"

inline void Register_qml_frame_define(QQmlContext *context)
{
#ifdef QT_DEBUG // 传递程序的构建类型，来隐藏部分Debug调试使用的功能
    context->setContextProperty("isDebugBuild", true);
#else
    context->setContextProperty("isDebugBuild", false);
#endif

    context->setContextProperty("PROJECT_NAME", PROJECT_NAME);
    context->setContextProperty("PROJECT_GIT_BRANCH", PROJECT_GIT_BRANCH);
    context->setContextProperty("PROJECT_SET_VERSION", PROJECT_SET_VERSION);
    context->setContextProperty("PROJECT_GIT_VERSION", PROJECT_GIT_VERSION);
    context->setContextProperty("PROJECT_TAG_VERSION", PROJECT_TAG_VERSION);
    context->setContextProperty("PORJECT_UPDATE_TIME", PORJECT_UPDATE_TIME);
    context->setContextProperty("BUILD_SYSTEM", BUILD_SYSTEM);
    context->setContextProperty("BUILD_SYSTEM_PROCESSOR", BUILD_SYSTEM_PROCESSOR);
    context->setContextProperty("BUILD_COMPILER_VERSION", BUILD_COMPILER_VERSION);
    context->setContextProperty("BUILD_DATE", BUILD_DATE);
    context->setContextProperty("SUPPORT_DEVELOPER", SUPPORT_DEVELOPER);
    context->setContextProperty("SUPPORT_OFFICIAL_DOMAIN", SUPPORT_OFFICIAL_DOMAIN);
    context->setContextProperty("SUPPORT_PROJECT_DOMAIN", SUPPORT_PROJECT_DOMAIN);
    context->setContextProperty("SUPPORT_FEEDBACK", SUPPORT_FEEDBACK);
    context->setContextProperty("SUPPORT_COPYRIGHT", SUPPORT_COPYRIGHT);


    // double PI = 3.1415926535897932;
    // double R2D= (180.0 / PI);
    // double D2R=(PI / 180.0);

    // context->setContextProperty("R2D", R2D);
    // context->setContextProperty("D2R",D2R);

}

