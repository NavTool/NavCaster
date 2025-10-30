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
    property string windowIcon: "qrc:/qt/qml/NavPost/res/logo.png"

    //显示的屏幕  /screen/xxxxx
    property string displayScreen: "/screen/init"  //主视窗显示内容

    //初始化页面显示内容
    property string displayInitScreen:"/page/start"             // 初始页面显示的页面

    //文件管理页面显示内容
    property string displayFileScreen:"/page/start"             // 文件管理页面显示的页面

    //主页面显示内容
    property string displayMidTop:"/page/blank"                 //Main视窗主页面显示内容（这个主要是记录状态，通过切换主页的页面都要通过open_page信号
    property string displayMidBottom:"/sidepage/log"            //Main视窗主页面底部显示内容
    property string displayLeftTop:"/gnss/sidepage/resource"    //Main视窗左上显示内容
    property string displayLeftBottom:"/sidepage/layer"         //Main视窗左上下页面显示内容
    property string displayRightTop:"/gnss/sidepage/property"   //Main视窗右上主页面显示内容
    property string displayRightBottom:"/sidepage/status"       //Main视窗右下主页面显示内容

    //顶部菜单栏
    property int navbarCurrentIndex:2   //主页面当前停留的菜单编号（从0开始），这边变量主要是为了保证当点击到文件页面后又返回的时候，能够切换回上一次选中的页面

    //打开对话框（发送信号，在Screen_Root中监听这个信号，并打开相应的Dialog
    signal open_dialog(string path,var args)
    //打开一个指定页面,新建页面，在Screen_Main中的监听这个信号，创建或切换到指定的页面（与displayMidTop在一个页面展示）
    signal open_page(string url,string key,string title)   //页面框架，页面key(UID)，页面标题
    signal close_page(string url,string key)   //页面框架，页面key(UID)，页面标题


    //主页页面可视控制
    property bool visable_header:true  //顶部菜单栏可视（顶部的那一排功能（文件、开始...））
    property bool visable_header_extra:true  //顶部菜单栏可视控制（顶部的具体功能页）
    property bool visable_footer:true  //底部状态栏可视控制（最底部那一排）
    //主要布局控制（左、中、右各两格）
    property bool visable_left_side:false //左侧可视（左状态栏）
    property bool visable_right_side:false //右侧可视（右状态栏）
    property bool visable_mid_side:true //中间可视（主页面+底部状态栏）
    property bool visable_left_top_side:false //左侧上部可视
    property bool visable_left_bottom_side:false //左侧下部可视
    property bool visable_mid_top_side:true //中间上部可视（主页面）
    property bool visable_mid_bottom_side:false //中间下部可视（日志栏）
    property bool visable_right_top_side:false //右侧上部可视
    property bool visable_right_bottom_side:false //右侧下部可视

    //当主页面可视化更新的时候，根据更新的状态来配置联动可视化状态
    function update_visable()
    {
        //根据六个子模块的可视性来确定三个大组（左、中、右）的可视性（
        if(visable_left_top_side || visable_left_bottom_side)
        {
            visable_left_side=true
        }
        else
        {
            visable_left_side=false
        }

        if(visable_right_top_side || visable_right_bottom_side)
        {
            visable_right_side=true
        }
        else
        {
            visable_right_side=false
        }

        if(visable_mid_top_side || visable_mid_bottom_side)
        {
            visable_mid_side=true
        }
        else
        {
            visable_mid_side=false
        }
    }


    Component.onCompleted: {
        // Theme.animationCurve=Easing.InOutQuad
    }


}
