cmake_minimum_required(VERSION 3.21)

project(CasterMonitor)

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#设置应用程序图标
if(WIN32)
    set(APPLICATION_DIR_PATH ${CMAKE_CURRENT_BINARY_DIR})
    set(APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/deploy/windows/icon.ico")
elseif(APPLE)
    set(APPLICATION_DIR_PATH ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.app/Contents/MacOS)
    set(APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/deploy/macOS/icon.icns")
else()
    set(APPLICATION_DIR_PATH ${CMAKE_CURRENT_BINARY_DIR})
    set(APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/deploy/linux/launcher.png")
endif()

set(VERSION_STR "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}.${VERSION_EXTRA}")
add_definitions(-DAPP_VERSION=${VERSION_STR})

set(QML_IMPORT_PATH ${CMAKE_SOURCE_DIR}/imports CACHE STRING "" FORCE)

find_package(Qt6 COMPONENTS Quick REQUIRED)

set(QT_SDK_DIR "${Qt6_DIR}/../../..")
cmake_path(SET QT_SDK_DIR NORMALIZE ${QT_SDK_DIR})

set(QML_FILES
# component

# dialog

# extra
# 扩展样式组件    
    qml/extra/DataGridEx.qml
    qml/extra/ExpanderEx.qml
    qml/extra/NavigationViewEx.qml
    qml/extra/PageRouterEx.qml
    qml/extra/PageRouterViewEx.qml
    qml/extra/TabViewEx.qml
    qml/extra/TreeDataGridEx.qml
# page
    qml/page/Page_Test.qml
# 初始页面
    qml/page/Init/Page_About.qml
    qml/page/Init/Page_Home.qml
    qml/page/Init/Page_Setting.qml
    qml/page/Init/Page_Start.qml
# Mointor页面
    qml/page/Monitor/Page_Client.qml
    qml/page/Monitor/Page_Event.qml
    qml/page/Monitor/Page_Exit.qml
    qml/page/Monitor/Page_Map.qml
    qml/page/Monitor/Page_Mpt_Alias.qml
    qml/page/Monitor/Page_Mpt_Nearest.qml
    qml/page/Monitor/Page_Mpt_Proxy.qml
    qml/page/Monitor/Page_Mpt_Pull.qml
    qml/page/Monitor/Page_Mpt_Push.qml
    qml/page/Monitor/Page_Mpt_Source.qml
    qml/page/Monitor/Page_Mpt_TCP.qml
    qml/page/Monitor/Page_Mpt.qml
    qml/page/Monitor/Page_Option.qml
    qml/page/Monitor/Page_Server.qml
    qml/page/Monitor/Page_Status.qml
    qml/page/Monitor/Page_User.qml

# plot

#reuse

# screen 页面框架
    qml/screen/Screen_Init.qml
    qml/screen/Screen_Main.qml
    qml/screen/Screen_Root.qml
# window 窗口框架
    qml/window/MainWindow.qml
    qml/window/PageWindow.qml
    qml/window/SingleInstanceWindow.qml
    qml/window/SingleTaskWindow.qml
    qml/window/StandardWindow.qml

    qml/App.qml
)

set(QML_SINGLETONS
    qml/Global.qml
    qml/Mointor.qml
    qml/Util.qml
)

set(RESOURCES_FILES
    res/logo.png
    CasterMonitor_zh_CN.qm
    CasterMonitor_en_US.qm
    res/bg_home_header.webp

    res/SinoGNSS2.png
    res/SinoGNSS.png

    res/image/StandardUICommand.png
    res/image/AnimatedIcon.png
    res/image/AppBarToggleButton.png
    res/image/Checkbox.png

    res/NavTool_ICON/icon.png
    res/NavTool_ICON/icon-0.png
    res/NavTool_ICON/icon-1.png
    res/NavTool_ICON/icon-2.png
    res/NavTool_ICON/icon-3.png
    res/NavTool_ICON/icon-4.png
    res/NavTool_ICON/icon-5.png
    res/NavTool_ICON/icon-6.png
)

include_directories(${PROJECT_NAME} ${CMAKE_BINARY_DIR})
set(SOURCE_FILES
    src/main.cpp
    src/stdafx.h
    src/frame.hpp
    src/AppInfo.h src/AppInfo.cpp
    src/Log.h src/Log.cpp
    src/CircularReveal.h src/CircularReveal.cpp
    src/PerformanceMonitor.h src/PerformanceMonitor.cpp
    src/SettingsHelper.h src/SettingsHelper.cpp
    src/Monitor/ClientDataController.h src/Monitor/ClientDataController.cpp
    src/Monitor/ServerDataController.h src/Monitor/ServerDataController.cpp
    src/Monitor/EventDataController.h src/Monitor/EventDataController.cpp
    src/Monitor/RelayDataController.h src/Monitor/RelayDataController.cpp
    src/Monitor/UserDataController.h src/Monitor/UserDataController.cpp
    src/Tool.h src/Tool.cpp
    src/CasterMonitor.h src/CasterMonitor.cpp
    )

set_source_files_properties(${QML_SINGLETONS} PROPERTIES QT_QML_SINGLETON_TYPE TRUE)


#根据不同的系统设置应用程序基本信息
if (WIN32)
    # configure_file(${CMAKE_CUTTRNT_SOURCE_DIR}/.cmake/win_app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/win_app.rc)
    # configure_file(${CMAKE_CUTTRNT_SOURCE_DIR}/.cmake/InstallerScript.iss.in ${CMAKE_SOURCE_DIR}/package/InstallerScript.iss)
    # set(APP_ICON_RESOURCE_WINDOWS ${CMAKE_CURRENT_BINARY_DIR}/win_app.rc)
    # qt_add_executable(${PROJECT_NAME} ${SOURCE_FILES} ${APP_ICON_RESOURCE_WINDOWS})
    set(APP_ICON_RESOURCE_WINDOWS ${CMAKE_CURRENT_SOURCE_DIR}/deploy/windows/info.rc)
    qt_add_executable(${PROJECT_NAME} ${SOURCE_FILES} ${APP_ICON_RESOURCE_WINDOWS})
elseif(APPLE)
    set(MACOSX_BUNDLE_GUI_IDENTIFIER ${PROJECT_DOMAIN})
    set(MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR})
    set(MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_NAME})
    set(MACOSX_BUNDLE_COPYRIGHT ${PROJECT_COPYRIGHT})
    set(MACOSX_BUNDLE_DEVELOPMENT_REGION "Chinese")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum OS X deployment version" FORCE)
    set(MACOSX_BUNDLE_EXECUTABLE_NAME ${PROJECT_NAME})
    set(MACOSX_BUNDLE_ICON_FILE favicon)
    configure_file(${CMAKE_SOURCE_DIR}/.cmake/MacOSXBundleInfo.plist.in ${CMAKE_CURRENT_BINARY_DIR}/Info.plist)
    set_source_files_properties(${APP_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    qt_add_executable(${PROJECT_NAME} ${SOURCE_FILES} ${APP_ICON})
elseif(UNIX)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/.cmake/linux_control.in ${CMAKE_SOURCE_DIR}/package/DEBIAN/control)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/.cmake/linux_postinst.in ${CMAKE_SOURCE_DIR}/package/DEBIAN/postinst)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/.cmake/linux_postrm.in ${CMAKE_SOURCE_DIR}/package/DEBIAN/postrm)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/.cmake/linux.desktop.in ${CMAKE_SOURCE_DIR}/package/opt/${PROJECT_NAME}/${PROJECT_NAME}.desktop)
    qt_add_executable(${PROJECT_NAME} ${SOURCE_FILES})
else()
    qt_add_executable(${PROJECT_NAME} ${SOURCE_FILES})
endif()

set(QT_QML_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/imports/)
qt_add_qml_module(${PROJECT_NAME}
    URI "CasterMonitor"
    VERSION "1.0"
    QML_FILES ${QML_FILES} ${QML_SINGLETONS}
    RESOURCES ${RESOURCES_FILES}
    RESOURCE_PREFIX /qt/qml
    # OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/imports/CasterMonitor
)

if (NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    find_program(QT_LUPDATE NAMES lupdate lupdate-qt6)
    find_program(QT_LRELEASE NAMES lrelease lrelease-qt6)
    file(GLOB TS_FILE_PATHS ${CMAKE_CURRENT_LIST_DIR}/ *.ts)
    add_custom_target(${PROJECT_NAME}_translations
        COMMAND ${QT_LUPDATE} ${CMAKE_CURRENT_LIST_DIR} -ts ${PROJECT_NAME}_en_US.ts WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        COMMAND ${QT_LUPDATE} ${CMAKE_CURRENT_LIST_DIR} -ts ${PROJECT_NAME}_zh_CN.ts WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        COMMAND ${QT_LRELEASE} ${PROJECT_NAME}_en_US.ts WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        COMMAND ${QT_LRELEASE} ${PROJECT_NAME}_zh_CN.ts WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        SOURCES ${TS_FILE_PATHS}
    )
add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}_translations)
endif()

include_directories(${PROJECT_NAME}  ${SPDLOG_INCLUDE_PATH})
include_directories(${PROJECT_NAME}  ${JSON_INCLUDE_PATH})


target_link_libraries(
    ${PROJECT_NAME} PRIVATE
    Qt6::Quick
    FluentUI
    FluentUIImpl
    FluentUIControls
    MonitorCore
)
if (FLUENTUI_BUILD_STATIC_LIB)
    target_link_libraries(
        ${PROJECT_NAME} PRIVATE
        FluentUIplugin
        FluentUIImplplugin
        FluentUIControlsplugin
    )
target_compile_definitions(${PROJECT_NAME} PRIVATE
    FLUENTUI_BUILD_STATIC_LIB
)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
    QT_MESSAGELOGCONTEXT
    PROJECT_NAME="${PROJECT_NAME}"
    PROJECT_COMPANY="${PROJECT_COMPANY}"
    PROJECT_DOMAIN="${PROJECT_DOMAIN}"
    PROJECT_DESCRIPTION="${PROJECT_DESCRIPTION}"
    PROJECT_VERSION="${PROJECT_VERSION}"
    PROJECT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    PROJECT_HOTLOAD_ENABLED="${HOTLOAD_ENABLED}"
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Monitor
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/Info.plist
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    OUTPUT_NAME ${PROJECT_NAME}
)

target_compile_definitions(${PROJECT_NAME} PRIVATE $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:QT_QML_DEBUG>)

set(DIST_PATH ${CMAKE_SOURCE_DIR}/dist)

file(WRITE ${CMAKE_BINARY_DIR}/remove_build_files.cmake
    "file(GLOB_RECURSE REMOVE_FILES
        \"${DIST_PATH}/*.qml\"
        \"${DIST_PATH}/*.debug\"
        \"${DIST_PATH}/*opengl32sw*\"
        \"${DIST_PATH}/*dxcompiler*\"
        \"${DIST_PATH}/*d3dcompiler_47*\"
        \"${DIST_PATH}/*vc_redist*\"
        \"${DIST_PATH}/*dxil*\"
        \"${DIST_PATH}/*Qt6Pdf*\"
        \"${DIST_PATH}/*Qt6VirtualKeyboard*\"
        \"${DIST_PATH}/*Qt6QuickControls2ImagineStyleImpl*\"
        \"${DIST_PATH}/*Qt6QuickControls2Universal*\"
        \"${DIST_PATH}/*Qt6QuickControls2UniversalStyleImpl*\"
        \"${DIST_PATH}/*Qt6QuickControls2WindowsStyleImpl*\"
        \"${DIST_PATH}/*Qt6QuickControls2Fusion*\"
        \"${DIST_PATH}/*Qt6QuickControls2FusionStyleImpl*\"
        \"${DIST_PATH}/*Qt6QuickControls2Imagine*\"
        \"${DIST_PATH}/*Qt6Quick3DUtils*\"
        \"${DIST_PATH}/*Qt63DAnimation*\"
        \"${DIST_PATH}/*Qt63DCore*\"
        \"${DIST_PATH}/*Qt63DExtras*\"
        \"${DIST_PATH}/*Qt63DInput*\"
        \"${DIST_PATH}/*Qt63DLogic*\"
        \"${DIST_PATH}/*Qt63DQuickScene2D*\"
        \"${DIST_PATH}/*Qt63DRender*\"
    )
    file(REMOVE \${REMOVE_FILES})
    "
)

if (CMAKE_BUILD_TYPE MATCHES "Release")
    if(WIN32)
        find_program(QT_DEPLOY NAMES windeployqt)
        if (MSVC)
            file(GLOB_RECURSE RUNTIME_FILE_PATHS ${CMAKE_SOURCE_DIR}/rely/runtime/msvc/*.dll)
        elseif (MINGW)
            file(GLOB_RECURSE RUNTIME_FILE_PATHS ${CMAKE_SOURCE_DIR}/rely/runtime/mingw/*.dll)
        endif ()

        # 获取所有 .dll 文件
        file(GLOB_RECURSE PROJECT_DLL_FILES "${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}/*.dll")

    add_custom_target(${PROJECT_NAME}_dist
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${DIST_PATH}
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.exe ${DIST_PATH}/${PROJECT_NAME}.exe
        # COMMAND ${QT_DEPLOY} --qmldir=${CMAKE_SOURCE_DIR} --plugindir ${DIST_PATH}/plugins --no-translations --compiler-runtime ${DIST_PATH}/${PROJECT_NAME}.exe
        COMMAND ${QT_DEPLOY} --qmldir=${CMAKE_SOURCE_DIR} --plugindir ${DIST_PATH}/plugins --compiler-runtime ${DIST_PATH}/${PROJECT_NAME}.exe
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/remove_build_files.cmake
        COMMAND ${CMAKE_COMMAND} -E copy ${RUNTIME_FILE_PATHS} ${DIST_PATH}
        COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_DLL_FILES} ${DIST_PATH}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}/data ${DIST_PATH}/data  #将Proj的data拷贝到目录
        COMMENT "Windows Deploying Qt Dependencies After Build........."
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
if (APPLE)
    find_program(QT_DEPLOY NAMES macdeployqt)
    add_custom_target(${PROJECT_NAME}_dist
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${DIST_PATH}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.app ${DIST_PATH}/${PROJECT_NAME}.app
        COMMAND ${QT_DEPLOY} ${DIST_PATH}/${PROJECT_NAME}.app -qmldir=${CMAKE_SOURCE_DIR}
        COMMAND find ${DIST_PATH}/${PROJECT_NAME}.app -name "*.dSYM" | xargs rm -r
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/remove_build_files.cmake
        COMMAND ${QT_DEPLOY} ${DIST_PATH}/${PROJECT_NAME}.app -dmg
        COMMENT "MacOs Deploying Qt Dependencies After Build........."
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
if (LINUX)

    #基于linuxdeploy_pack.sh.in 生成 linuxdeploy_pack.sh

    set(DEPLOY_TARGET ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}/${PROJECT_NAME})
    set(EXTRA_DIR ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE})

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/.cmake/linuxdeploy_pack.sh.in ${CMAKE_SOURCE_DIR}/bin/linuxdeploy_pack.sh @ONLY)


    # add_custom_target(${PROJECT_NAME}_dist
    #     COMMAND bash  "set -x; ${CMAKE_SOURCE_DIR}/bin/linuxdeploy_pack.sh"
    #     COMMENT "LINUX Deploying Qt Dependencies After Build........."
    #     SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    #     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    # )
endif()
endif()


