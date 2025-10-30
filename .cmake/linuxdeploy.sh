#!/bin/sh 
# linux的打包脚本
#放置到build目录下

#传递参数

#QT的路径
QT_SDK_PATH="@QT_SDK_PATH@"
#图标的路径
ICON_PATH="@ICON_PATH@"
#工程的路径
CMAKE_SOURCE_DIR="@CMAKE_SOURCE_DIR@"
#要打包的程序路径
DIST_PATH="@DIST_PATH@"




#设置Qt的环境变量
export QT_DIR=QT_SDK_PATH
export PATH=${QT_DIR}/bin:$PATH
export LIB_PATH=${QT_DIR}/lib:$LIB_PATH
export PLUGIN_PATH=${QT_DIR}/plugins:$PLUGIN_PATH
export QML2_PATH=${QT_DIR}/qml:$QML2_PATH
export LD_LIBRARY_PATH=${QT_DIR}/lib:$LD_LIBRARY_PATH 
export QT_PLUGIN_PATH=${QT_DIR}/plugins:$PLUGIN_PATH 

#设置工程的环境变量
export QML_SOURCES_PATHS=CMAKE_SOURCE_DIR

echo "[INFO] Qt SDK Path: $QT_SDK_PATH"
echo "[INFO] Source Dir : $CMAKE_SOURCE_DIR"
echo "[INFO] Dist Path  : $DIST_PATH"


#检测Linux_deploy是否存在

LINUX_DEPLOY="$CMAKE_SOURCE_DIR/env/x86_64/"





