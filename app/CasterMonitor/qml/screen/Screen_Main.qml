import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import FluentUI.Controls
import FluentUI.impl
import CasterMonitor
import "../extra"

Item{

    id:root

    property string title
    property PageContext context

    property var colors : [Colors.yellow,Colors.orange,Colors.red,Colors.magenta,Colors.purple,Colors.blue,Colors.teal,Colors.green]


    Component.onCompleted:{
        window.setHitTestVisible(top_bar_left) //设置组件id来顶部的按键可以使用
        // window.setHitTestVisible(top_bar_right) //设置组件id来顶部的按键可以使用
        window.setHitTestVisible(top_bar_mid)
    }


    //主页面导航栏
    ListModel{
        id: body_mid_info_model
        ListElement{url:"/page/blank";key:"";title:qsTr("起始页");icon:""}
        ListElement{url:"/page/map";key:"";title:qsTr("平面视图");icon:""}
        ListElement{url:"/page/task";key:"";title:qsTr("任务队列");icon:""}

        ListElement{url:"/gnss/page/resource";key:"";title:qsTr("GNSS");icon:""}
        ListElement{url:"/gnss/page/data/spanchart";key:"";title:qsTr("数据区间");icon:""}
        ListElement{url:"/gnss/page/quality/resultchart";key:"";title:qsTr("质量绘图");icon:""}
        ListElement{url:"/gnss/page/quality/resulttable";key:"";title:qsTr("质量结果");icon:""}
        ListElement{url:"/gnss/page/solution/resultchart";key:"";title:qsTr("结果绘图");icon:""}
        ListElement{url:"/gnss/page/solution/resulttable";key:"";title:qsTr("解算结果");icon:""}

        ListElement{url:"/ins/page/mapview";key:"";title:qsTr("组合导航");icon:""}
        ListElement{url:"/ins/page/resource";key:"";title:qsTr("INS");icon:""}
        ListElement{url:"/ins/page/solution/resultchart";key:"";title:qsTr("结果绘图");icon:""}
        ListElement{url:"/ins/page/solution/resulttable";key:"";title:qsTr("解算结果");icon:""}

        ListElement{url:"/rtk/page/resource";key:"";title:qsTr("RTK");icon:""}


        ListElement{url:"/test/page/test";key:"";title:qsTr("功能测试");icon:""}
    }

    //右测详情栏
    PageRouter{
        id: body_mid_router
        routes: {
            "/monitor/page/resource": R.resolvedUrl("qml/page/Monitor/Page_Resource.qml"),
        }
    }
    //右测详情栏
    PageRouter{
        id: body_right_router
        routes: {
            "/sidepage/property": R.resolvedUrl("qml/component/SidePage_Property.qml"),
            "/gnss/sidepage/property": R.resolvedUrl("qml/component/GNSS/SidePage_Property.qml"),
        }
    }


    property list<QtObject> originalItems : [
        PaneItem{
            key: "/gnss/page/resource/station"
            title: "站点"
            icon.name: FluentIcons.graph_MapPin
            icon.color:  Theme.res.textFillColorPrimary
        },
        PaneItem{
            key: "/gnss/page/resource/controlpoint"
            title: "控制点"
            icon.name: FluentIcons.graph_POI
            icon.color:  Theme.res.textFillColorPrimary
        },
        PaneItem{
            key: "/gnss/page/resource/obsfile"
            title: "观测文件"
            icon.name: FluentIcons.graph_Page
            icon.color:  Theme.res.textFillColorPrimary
        },
        PaneItem{
            key: "/gnss/page/resource/vector"
            title: "静态基线"
            icon.name: FluentIcons.graph_ResizeTouchLarger
            icon.color:  Theme.res.textFillColorPrimary
        }

    ]
    property list<QtObject> originalFooterItems : [
        PaneItem{
            icon.name: FluentIcons.graph_FavoriteList
            icon.color:  Theme.res.textFillColorPrimary
            key: "/gnss/page/resource/coord"
            title: qsTr("坐标(正式版需隐藏)")
        },
        PaneItem{
            icon.name: FluentIcons.graph_Settings
            icon.color:  Theme.res.textFillColorPrimary
            key: "/gnss/page/resource/option"
            title: qsTr("选项")
        }
    ]
    PageRouter{
        id: page_router
        routes: {
            "/gnss/page/resource/station":{url: R.resolvedUrl("qml/page/GNSS/Page_Resource_Station.qml"),singleton:true},
            "/gnss/page/resource/coord":{url: R.resolvedUrl("qml/page/GNSS/Page_Resource_Coord.qml"),singleton:true},
            "/gnss/page/resource/controlpoint":{url: R.resolvedUrl("qml/page/GNSS/Page_Resource_ControlPoint.qml"),singleton:true},
            "/gnss/page/resource/obsfile":{url: R.resolvedUrl("qml/page/GNSS/Page_Resource_Obsfile.qml"),singleton:true},
            "/gnss/page/resource/vector":{url: R.resolvedUrl("qml/page/GNSS/Page_Resource_Vector.qml"),singleton:true},
            "/gnss/page/resource/baseline":{url: R.resolvedUrl("qml/page/GNSS/Page_Resource_Baseline.qml"),singleton:true},
            "/gnss/page/resource/quality":{url: R.resolvedUrl("qml/page/GNSS/Page_Resource_Quality.qml"),singleton:true},
            "/gnss/page/resource/closeloop":{url: R.resolvedUrl("qml/page/GNSS/Page_Resource_Closeloop.qml"),singleton:true},
            "/gnss/page/resource/solution":{url: R.resolvedUrl("qml/page/GNSS/Page_Resource_Solution.qml"),singleton:true},
            "/gnss/page/resource/navfile":{url: R.resolvedUrl("qml/page/GNSS/Page_Resource_Navfile.qml"),singleton:true},
            "/gnss/page/resource/option":{url: R.resolvedUrl("qml/page/GNSS/Page_Resource_Option.qml"),singleton:true}
        }
    }


    //用用主页面：
    Item{
        anchors{
            left:parent.left
            right:parent.right
            top:parent.top
            topMargin:45
            bottom:parent.bottom
        }

        //主页主要框架栏
        Item{
            id:body
            anchors{
                top:parent.top
                bottom:footer.top
                left:parent.left
                right:parent.right
                topMargin:5
                leftMargin:5
                rightMargin:5
            }
            width:parent.width

            SplitView {
                id:split_layout
                anchors.fill: parent
                orientation: Qt.Horizontal


                Item {
                    clip: true
                    visible:Global.visable_mid_side
                    SplitView.fillWidth: true
                    SplitView.fillHeight: true

                    NavigationView{
                        anchors{
                            left:parent.left
                            right:parent.right
                            top:parent.top
                            topMargin:45
                            bottom:parent.bottom
                        }
                        //logo: "qrc:/qt/qml/Gallery/res/image/logo.png"
                        //title: "FluentUI Gallery"
                        router: page_router
                        items: originalItems
                        footerItems: originalFooterItems
                        displayMode: NavigationViewType.Top

                        sideBarShadow: false
                        goBackButton.visible: false
                        appBarHeight: 5
                        sideBarWidth:180
                        // sideItemHeight:35


                        onTap:
                            (item)=>{
                                if(item.key){
                                    page_router.go(item.key,{info:item.title})
                                }
                            }
                        Component.onCompleted: {
                            page_router.go(Monitor.displayResourcePage)
                        }

                        Connections{
                            target:Monitor
                            function onDisplayResourcePageChanged(){
                                page_router.go(Monitor.displayResourcePage)
                            }
                        }

                    }

                }

                Item {
                    clip: true
                    visible:Global.visable_right_side
                    implicitWidth: body.width*0.2>300?300:body.width*0.2
                    implicitHeight: body.height


                    PageRouterView{
                        id: right_panne
                        anchors.fill: parent
                        //anchors.topMargin: header_extra.visible?header_extra.height:0
                        router: body_right_router
                        clip: true
                        Component.onCompleted: {
                            body_right_router.go(Global.displayMainScreenEx)
                        }

                        Connections{
                            target:Global
                            function onDisplayMainScreenExChanged(){
                                body_right_router.go(Global.displayMainScreenEx)
                            }
                        }
                    }


                }
            }
        }

        //页面底部状态栏
        Item{
            id:footer

            anchors{
                bottom:parent.bottom
            }
            width:parent.width
            height:25

            Menu {
                id:footer_menu
                width:180

                MenuItem{
                    icon.name: FluentIcons.graph_GlobalNavButton
                    text:Global.visable_header_extra?qsTr("Hide Top Bar"): qsTr("Show Top Bar")

                    onTriggered:{
                        Global.visable_header_extra=!Global.visable_header_extra
                    }
                }

                MenuItem{
                    icon.name:Global.visable_header?FluentIcons.graph_CheckMark:FluentIcons.graph_CheckboxIndeterminate
                    text:qsTr("Show Menu Bar")

                    onTriggered:{
                        Global.visable_header=!Global.visable_header

                        if(Global.visable_header==false)
                        {
                            Global.visable_header_extra=false
                        }
                    }
                }

                MenuSeparator { }

                MenuItem{
                    icon.name: FluentIcons.graph_ResizeMouseTall
                    text:Global.visable_left_side?qsTr("Hide Left Sidebar"): qsTr("Show Left Sidebar")

                    onTriggered:{
                        Global.visable_left_side=!Global.visable_left_side
                    }
                }
                MenuItem{
                    icon.name:  FluentIcons.graph_ResizeMouseTall
                    text: Global.visable_right_side?qsTr("Hide Right Sidebar"): qsTr("Show Right Sidebar")

                    onTriggered:{
                        Global.visable_right_side=!Global.visable_right_side
                    }
                }

                MenuItem{
                    icon.name:  FluentIcons.graph_ResizeMouseWide
                    text: Global.visable_mid_bottom_side?qsTr("Hide Bottom Bar"): qsTr("Show Bottom Bar")

                    onTriggered:{
                        Global.visable_mid_bottom_side=!Global.visable_mid_bottom_side
                    }
                }

                MenuSeparator { }


                MenuItem{
                    icon.name:  FluentIcons.graph_Settings
                    text:qsTr("Settings")
                    onTriggered:{
                    }
                }

                MenuItem{
                    icon.name:  FluentIcons.graph_Info
                    text:qsTr("About")
                    onTriggered:{
                    }
                }

                MenuSeparator { }
                MenuItem{
                    icon.name:  FluentIcons.graph_More
                    text:qsTr("More")
                    onTriggered:{
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton  // 仅接受右键点击

                onPressed: (mouse) => {
                               if (mouse.button === Qt.RightButton) {
                                   // 显示菜单，并设置菜单显示位置为鼠标点击的位置
                                   footer_menu.open();
                                   footer_menu.x = mouse.x;
                                   footer_menu.y = mouse.y;
                               }
                           }
            }



            // 右侧按钮，从右向左排列
            RowLayout {
                id:foot_bar_right

                anchors
                {
                    left:parent.left
                    leftMargin:0
                    verticalCenter: parent.verticalCenter
                }

                spacing: 0

                // 设置 LayoutMirroring 使其从右向左排列
                LayoutMirroring.enabled: true
                LayoutMirroring.childrenInherit: true
                IconButton{
                    text: "Fold"
                    icon.name: FluentIcons.graph_ResizeMouseTall
                    icon.width: 18
                    icon.height: 18
                    spacing: 5
                    display: IconButton.IconOnly
                    rotation:180
                    icon.color:"Grey"

                    property bool pre_right_top_visible
                    property bool pre_right_bottom_visible

                    Component.onCompleted:
                    {
                        pre_right_top_visible=Global.visable_right_top_side
                        pre_right_bottom_visible==Global.visable_right_bottom_side
                    }

                    onClicked:{
                        Global.visable_right_side=!Global.visable_right_side
                        if(Global.visable_right_side)
                        {
                            if(!Global.visable_right_top_side && !Global.visable_right_bottom_side)
                            {
                                Global.visable_right_top_side=pre_right_top_visible;
                                Global.visable_right_bottom_side=pre_right_bottom_visible;
                            }
                        }
                        else
                        {
                            pre_right_top_visible=Global.visable_right_top_side
                            pre_right_bottom_visible=Global.visable_right_bottom_side
                            Global.visable_right_top_side=false
                            Global.visable_right_bottom_side=false
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.RightButton  // 仅接受右键点击

                        onPressed: (mouse) => {
                                       if (mouse.button === Qt.RightButton) {
                                           // 显示菜单，并设置菜单显示位置为鼠标点击的位置
                                           footer_right_menu.open();
                                           footer_right_menu.x = mouse.x;
                                           footer_right_menu.y = mouse.y;
                                       }
                                   }
                    }

                    Menu {
                        id:footer_right_menu
                        width:180

                        MenuItem{
                            icon.name:Global.visable_right_top_side?FluentIcons.graph_CheckMark:FluentIcons.graph_CheckboxIndeterminate
                            text:qsTr("Show Right Top Bar")

                            onTriggered:{
                                Global.visable_right_top_side=!Global.visable_right_top_side
                                Global.update_visable()
                            }
                        }
                        MenuItem{
                            icon.name:Global.visable_right_bottom_side?FluentIcons.graph_CheckMark:FluentIcons.graph_CheckboxIndeterminate
                            text:qsTr("Show Right Bottom Bar")

                            onTriggered:{
                                Global.visable_right_bottom_side=!Global.visable_right_bottom_side
                                Global.update_visable()
                            }
                        }
                    }
                }


                PerformanceMonitor{
                    id:fps_item
                }

                Text{
                    text: "fps %1".arg(fps_item.fps)
                    opacity: 0.3
                }
            }

        }
    }

    //应用顶部：左侧按钮，从左到右排列
    RowLayout {
        // id:top_bar_action

        clip: true

        anchors
        {
            top:parent.top
            left:parent.left
            topMargin:0
            leftMargin:5
            right:parent.right
            rightMargin:250
        }

        //左上角应用名称

        spacing:0

        Row
        {
            leftPadding: 10
            // topPadding:

            Layout.alignment: Qt.AlignLeft|Qt.AlignTop
            Layout.topMargin: 10

            Image{
                source: Global.windowIcon
                width: 20
                height: 20
                fillMode: Image.PreserveAspectFit
                anchors.verticalCenter: parent.verticalCenter
            }

            Item {
                width: 10
                height:1
            }

            Label{
                text:Global.windowName+" "+ PROJECT_SET_VERSION
                font: Typography.bodyStrong

                anchors.verticalCenter: parent.verticalCenter
            }

            Item {
                width: 20
                height:1
            }

            Label{
                text: qsTr("自动保存")
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Row
        {
            id:top_bar_left
            Layout.alignment:Qt.AlignTop
            Layout.topMargin:3

            Switch{
                id: switch_disabled

                anchors.verticalCenter: parent.verticalCenter

                onCheckedChanged:{
                    if(checked){
                        tip_top.show(InfoBarType.Success,qsTr("自动保存已开启!"))
                    }
                    else{
                        tip_top.show(InfoBarType.Info,qsTr("自动保存已关闭!"))
                    }
                }

                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: qsTr("自动保存")
            }

            IconButton{
                text: qsTr("保存")
                icon.name: FluentIcons.graph_Save
                icon.width: 18
                icon.height: 18
                spacing: 5
                display: IconButton.IconOnly
                anchors.verticalCenter: parent.verticalCenter

                onClicked:{
                    tip_top.show(InfoBarType.Success,qsTr("The project has been saved!"))
                }

                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: text
            }

            // IconButton{
            //     text: "Undo"
            //     icon.name: FluentIcons.graph_Undo
            //     icon.width: 18
            //     icon.height: 18
            //     spacing: 5
            //     display: IconButton.IconOnly
            //     anchors.verticalCenter: parent.verticalCenter

            //     ToolTip.visible: hovered
            //     ToolTip.delay: 500
            //     ToolTip.text: text

            //     onClicked:{
            //         Global.displayScreen=1
            //     }
            // }

            // IconButton{
            //     text: "Rndo"
            //     icon.name: FluentIcons.graph_Redo
            //     icon.width: 18
            //     icon.height: 18
            //     spacing: 5
            //     display: IconButton.IconOnly
            //     anchors.verticalCenter: parent.verticalCenter

            //     ToolTip.visible: hovered
            //     ToolTip.delay: 500
            //     ToolTip.text: text
            //     onClicked:{
            //         Global.displayScreen=2
            //     }
            // }

            IconButton{
                text: qsTr("刷新")
                icon.name: FluentIcons.graph_Refresh
                icon.width: 18
                icon.height: 18
                spacing: 5
                display: IconButton.IconOnly
                anchors.verticalCenter: parent.verticalCenter

                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: text

                onClicked:
                {
                    GNSS_API.updateContext()
                    GnssResourceController.loadData()
                }

            }
        }
        // 左 -> 中 的弹性空白
        Item { Layout.fillWidth: true }



        Item{

            id:top_bar_mid

            Layout.preferredWidth: 320      // 默认宽度
            Layout.minimumWidth: 200        // 最小宽度，支持缩小
            Layout.maximumWidth: 320       // 可选：最大宽度
            Layout.fillWidth: true          // 允许填满剩余空间

            height: 30

            IconButton{
                anchors.fill: parent
                Row{
                    anchors.centerIn: parent

                    spacing: 10

                    Label{
                        text:"未创建工程"
                        font: Typography.bodyStrong

                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Label{
                        text:"  -  "
                        font: Typography.bodyStrong

                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Label{
                        text:"不可用"
                        font: Typography.bodyStrong
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Icon
                    {
                        anchors.verticalCenter: parent.verticalCenter
                        source:FluentIcons.graph_ChevronDown
                        width: 15
                        height: 15
                        // spacing: 0
                    }

                }
            }
        }

        // ExpanderEx{
        //     id:top_bar_mid

        //         Layout.preferredWidth: 480      // 默认宽度
        //         Layout.minimumWidth: 200        // 最小宽度，支持缩小
        //         Layout.maximumWidth: 480        // 可选：最大宽度
        //     // Layout.fillWidth: true          // 允许填满剩余空间


        //     property string dispaly_project_name:qsTr("未创建工程")
        //     property string dispaly_project_statue:qsTr("不可用")
        //     property string dispaly_project_path:qsTr("不可用")
        //     property string dispaly_project_work_path:qsTr("不可用")
        //     property string dispaly_project_creation_date:qsTr("不可用")
        //     property string dispaly_project_modification_date:qsTr("不可用")

        //     expanderHeight: 40

        //     implicitWidth: 480

        //     anchors{
        //         // top:parent.top
        //         // left:top_bar_left.right
        //     }

        //     leading:Row{
        //         anchors.verticalCenter:  parent.verticalCenter
        //         anchors.horizontalCenter: parent.horizontalCenter
        //         Label{
        //             // width:170
        //             height:top_bar_mid.expanderHeight
        //             text: top_bar_mid.dispaly_project_statue
        //             horizontalAlignment: Qt.AlignHCenter
        //             verticalAlignment: Qt.AlignVCenter
        //         }
        //     }

        //     header:Row{
        //         anchors.verticalCenter:  parent.verticalCenter
        //         anchors.horizontalCenter:   parent.horizontalCenter
        //         Label{
        //             width:170
        //             height:top_bar_mid.expanderHeight
        //             text:  top_bar_mid.dispaly_project_name
        //             verticalAlignment: Qt.AlignVCenter
        //             horizontalAlignment: Text.AlignHCenter
        //             // verticalAlignment: Text.AlignVCenter
        //             elide: Text.ElideMiddle  // 超出时在右侧显示省略号
        //         }
        //         Label{
        //             // width:170
        //             height:top_bar_mid.expanderHeight
        //             text: "  -  "
        //             horizontalAlignment: Qt.AlignHCenter
        //             verticalAlignment: Qt.AlignVCenter
        //         }
        //     }

        //     content:Frame{
        //         background:Rectangle {
        //             color: Theme.res.popupBackgroundColor
        //             //Theme.res.acrylicBackgroundColor
        //             //Theme.res.inactiveBackgroundColor
        //             //Theme.res.micaBackgroundColor
        //             //Theme.res.popupBackgroundColor
        //             //Theme.res.scaffoldBackgroundColor
        //             border.color: control.palette.mid

        //             radius: 10
        //         }

        //         Column{
        //             padding: 10
        //             bottomPadding: 20
        //             spacing: 10
        //             Label{
        //                 text: qsTr("工程名")
        //             }
        //             TextBox{
        //                 height: 30
        //                 width: 300
        //                 text: project_info.dispaly_project_name
        //                 enabled: false

        //                 trailing: Label{
        //                     text: qsTr(".nps ")
        //                     // padding: 0
        //                     color: Theme.res.textFillColorDisabled
        //                 }
        //             }
        //             Label{
        //                 text: qsTr("位置")
        //                 // color:
        //             }
        //             MultiLineTextBox{
        //                 // height: 70
        //                 width: 300
        //                 text: project_info.dispaly_project_work_path
        //                 wrapMode: Text.WordWrap
        //                 enabled: false

        //                 leading: IconButton{
        //                     implicitWidth: 30
        //                     implicitHeight: 20
        //                     icon.name: FluentIcons.graph_Folder
        //                     icon.width: 30
        //                     icon.height: 30
        //                     padding: 0
        //                 }
        //             }
        //             Label{
        //                 text: qsTr("工程创建日期")
        //             }

        //             TextBox{
        //                 height: 30
        //                 width: 300
        //                 text: project_info.dispaly_project_creation_date
        //                 enabled: false
        //             }
        //             Label{
        //                 text: qsTr("最近修改日期")
        //             }
        //             TextBox{
        //                 height: 30
        //                 width: 300
        //                 text: project_info.dispaly_project_modification_date
        //                 enabled: false
        //             }
        //         }
        //     }

        //     Connections{
        //         target: NavPost
        //         function onRefreshProjectInfoSuccess(){
        //             project_info.dispaly_project_name=NavPost.project_info.project_name
        //             project_info.dispaly_project_statue= NavPost.project_info.is_modified?qsTr("未保存"):qsTr("已保存")
        //             project_info.dispaly_project_path=NavPost.project_info.project_path
        //             project_info.dispaly_project_work_path=NavPost.project_info.project_work_path

        //             var creation_date = new Date(NavPost.project_info.project_creation_date * 1000); // 转换为毫秒并创建 Date 对象
        //             var  modification_date=new Date(NavPost.project_info.project_modification_date * 1000); // 转换为毫秒并创建 Date 对象
        //             project_info.dispaly_project_creation_date=creation_date.toLocaleString()
        //             project_info.dispaly_project_modification_date=modification_date.toLocaleString()
        //         }
        //     }

        //     Component.onCompleted: {
        //         NavPost.refreshProjectInfo()
        //     }
        // }





        // TextBox{
        //     id:top_bar_right


        //     Layout.preferredWidth: 200      // 默认宽度
        //     Layout.minimumWidth: 100        // 最小宽度，支持缩小
        //     Layout.maximumWidth: 200        // 可选：最大宽度
        //     Layout.fillWidth: true          // 允许填满剩余空间

        //     Layout.alignment: Qt.AlignTop
        //     Layout.rightMargin: 20
        //     placeholderText: "搜索"
        //     trailing: IconButton{
        //         implicitWidth: 30
        //         implicitHeight: 20
        //         icon.name: FluentIcons.graph_Search
        //         icon.width: 14
        //         icon.height: 14
        //         padding: 0
        //     }

        //     focusReason:Qt.MouseFocusReason

        // }


        //  中 -> 右 的弹性空白
        Item { Layout.fillWidth: true }



    }




}
