pragma Singleton

import QtQuick
import QtQuick.Controls
import FluentUI.Controls
import FluentUI.impl
import CasterMonitor

/*

导航路由说明（路由表，结构分配）

/screen  视图路由（零级路由，位于Screen_Root.qml)
    /screen/init
    /screen/main
    /screen/file

（一级路由，位于Screen_Main.qml)
/page    主页面路由
    /page/map
    /page/table                     在page_table.qml
        /page/table/station
        /page/table/obsfile
        /page/table/baseline
        /page/table/check
        /page/table/closeloop
        /page/table/option
    /page/span
/navbar   导航栏路由
    navbar/start
/sidepage  侧边栏路由
    navbar/property/
        navbar/property/station



（一级路由，位于Screen_Main.qml)



（一级路由，位于Screen_File.qml)


*/


QtObject {
    id: control
    property var starter
    property int displayMode: NavigationViewType.Auto
    property int windowEffect: WindowEffectType.Normal  //WindowEffectType.Mica

    property bool debugMode:false

    property var windowName: PROJECT_NAME
    property string windowIcon: "qrc:/qt/qml/CasterMonitor/res/logo.png"

    //显示的屏幕  /screen/xxxxx
    property string displayScreen: "/screen/init"  //主视窗显示内容

    //初始化页面显示内容
    property string displayInitScreen:"/init/page/home"             // 初始页面显示的页面
    property string displayMainScreen:"/monitor/page/status"                //Main视窗主页面显示内容（这个主要是记录状态，通过切换主页的页面都要通过open_page信号


    //打开对话框（发送信号，在Screen_Root中监听这个信号，并打开相应的Dialog
    signal open_dialog(string path,var args)
    //打开一个指定页面,新建页面，在Screen_Main中的监听这个信号，创建或切换到指定的页面（与displayMidTop在一个页面展示）
    signal open_page(string url,string key,string title)   //页面框架，页面key(UID)，页面标题
    signal close_page(string url,string key)   //页面框架，页面key(UID)，页面标题


    //主页页面可视控制
    //主要布局控制（左、中、右各两格）
    property bool visable_right_side:false //右侧可视（右状态栏）




}
