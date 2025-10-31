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
        // window.setHitTestVisible(navigation_view.logoDelegate)  //设置组件id来顶部的按键可以使用
    }


    //右测栏
    PageRouter{
        id: body_right_router
        routes: {
            "/sidepage/property": R.resolvedUrl("qml/component/SidePage_Property.qml"),
            "/gnss/sidepage/property": R.resolvedUrl("qml/component/GNSS/SidePage_Property.qml"),
        }
    }

    property list<QtObject> originalItems : [
        PaneItem{
            key: "/monitor/page/status"
            title: "节点状态"
            icon.name: FluentIcons.graph_ViewAll
            icon.color:  Theme.res.textFillColorPrimary
        },
        PaneItem{
            key: "/monitor/page/server"
            title: "基准站"
            icon.name:FluentIcons.graph_MapPin
            icon.color:  Theme.res.textFillColorPrimary
        },
        PaneItem{
            key: "/monitor/page/client"
            title: "移动站"
            icon.name: FluentIcons.graph_POI
            icon.color:  Theme.res.textFillColorPrimary
        },
        PaneItem{
            key: "/monitor/page/user"
            title: "用户管理"
            icon.name: FluentIcons.graph_People
            icon.color:  Theme.res.textFillColorPrimary
        },
        PaneItem{
            key: "/monitor/page/mpt"
            title: "挂载点管理"
            icon.name: FluentIcons.graph_Devices
            icon.color:  Theme.res.textFillColorPrimary
        }
        ,PaneItem{
            key: "/monitor/page/map"
            title: "地图"
            icon.name: FluentIcons.graph_TiltUp
            icon.color:  Theme.res.textFillColorPrimary
        },
        PaneItem{
            key: "/monitor/page/event"
            title: "事件管理"
            icon.name: FluentIcons.graph_Event12
            icon.color:  Theme.res.textFillColorPrimary
        }

    ]
    property list<QtObject> originalFooterItems : [
        PaneItem{
            icon.name: FluentIcons.graph_FavoriteList
            icon.color:  Theme.res.textFillColorPrimary
            key: "/monitor/page/test"
            title: qsTr("测试(正式版需隐藏)")
        },
        PaneItem{
            icon.name: FluentIcons.graph_Settings
            icon.color:  Theme.res.textFillColorPrimary
            key: "/monitor/page/option"
            title: qsTr("选项")
        },
        PaneItem{
            icon.name: FluentIcons.graph_ClosePane
            icon.color:  Theme.res.textFillColorPrimary
            key: "/monitor/page/exit"
            title: qsTr("退出")
        }
    ]
    PageRouter{
        id: page_router
        routes: {
            "/monitor/page/client":{url: R.resolvedUrl("qml/page/Monitor/Page_Client.qml"),singleton:true},
            "/monitor/page/event":{url: R.resolvedUrl("qml/page/Monitor/Page_Event.qml"),singleton:true},
            "/monitor/page/exit":{url: R.resolvedUrl("qml/page/Monitor/Page_Exit.qml"),singleton:true},
            "/monitor/page/map":{url: R.resolvedUrl("qml/page/Monitor/Page_Map.qml"),singleton:true},
            "/monitor/page/mpt":{url: R.resolvedUrl("qml/page/Monitor/Page_Mpt.qml"),singleton:true},
            "/monitor/page/option":{url: R.resolvedUrl("qml/page/Monitor/Page_Option.qml"),singleton:true},
            "/monitor/page/server":{url: R.resolvedUrl("qml/page/Monitor/Page_Server.qml"),singleton:true},
            "/monitor/page/status":{url: R.resolvedUrl("qml/page/Monitor/Page_Status.qml"),singleton:true},
            "/monitor/page/test":{url: R.resolvedUrl("qml/page/Monitor/Page_Test.qml"),singleton:true},
            "/monitor/page/user":{url: R.resolvedUrl("qml/page/Monitor/Page_User.qml"),singleton:true},
        }
    }

    //标题栏
    Component{
        id: comp_profile
        Item {
            // id:top_bar_action

            width: root.width
            height: 35

            clip: true

            Row
            {
                //左上角应用图标
                Image{
                    source: Global.windowIcon
                    width: 25
                    height: 25
                    fillMode: Image.PreserveAspectFit
                    anchors.verticalCenter: parent.verticalCenter
                }

                Item {
                    width: 10
                    height:1
                }
                //左上角应用名称
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
                    text: qsTr("连接节点")
                    anchors.verticalCenter: parent.verticalCenter
                }

                Switch{
                    id: switch_disabled
                    anchors.verticalCenter: parent.verticalCenter
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: qsTr("连接节点")

                    onCheckedChanged:{
                        if(checked){
                            tip_top.show(InfoBarType.Success,qsTr("已成功连接至节点网络!"))
                        }
                        else{
                            tip_top.show(InfoBarType.Info,qsTr("已从节点网络断开连接!"))
                        }
                    }

                    Component.onCompleted:
                    {
                        window.setHitTestVisible(this)  //设置此对象可以穿透顶部栏
                    }
                }
                IconButton{
                    text: qsTr("刷新")
                    icon.name: FluentIcons.graph_Refresh
                    icon.width: 16
                    icon.height: 16
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

                    Component.onCompleted:
                    {
                        window.setHitTestVisible(this)  //设置此对象可以穿透顶部栏
                    }
                }
            }
            IconButton{
                anchors.horizontalCenter: parent.horizontalCenter
                width: 320
                height: 30
                Row{
                    anchors.centerIn: parent

                    spacing: 10

                    Label{
                        text:"81.68.72.44"
                        font: Typography.bodyStrong

                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Label{
                        text:"  -  "
                        font: Typography.bodyStrong

                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Label{
                        text:"已连接"
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


                Component.onCompleted:
                {
                    window.setHitTestVisible(this)  //设置此对象可以穿透顶部栏
                }
            }
        }
    }


    //主页主要框架栏
    Item{
        id:body
        anchors{
            top:parent.top
            bottom:footer.top
            left:parent.left
            right:parent.right
            // topMargin:5
            leftMargin:5
            rightMargin:5
        }
        width:parent.width

        NavigationView{
            id: navigation_view
            router: page_router
            anchors.fill: parent
            // logo: Global.windowIcon
            // title: Global.windowName +" v."+ PROJECT_SET_VERSION
            items: originalItems
            footerItems: originalFooterItems
            displayMode: NavigationViewType.Top
            appBarHeight: Qt.platform.os === "osx" ? 60 : 48
            titleBarTopMargin: Qt.platform.os === "osx" ? 20 : 0
            topBarHeight:40
            goBackButton.visible: false
            // sideItemDelegate: Label{
            //     property var model
            //     text: model.title
            // }

            logoDelegate:comp_profile

            // autoSuggestBox: AutoSuggestBox{
            //     id: auto_suggset_search
            //     placeholderText: qsTr("Search")
            //     items: []
            //     textRole: "title"
            //     trailing: RowLayout{
            //         IconButton{
            //             implicitWidth: 30
            //             implicitHeight: 20
            //             icon.name: FluentIcons.graph_ChromeClose
            //             icon.width: 10
            //             icon.height: 10
            //             visible: auto_suggset_search.text !== ""
            //             onClicked: {
            //                 auto_suggset_search.clear()
            //             }
            //         }
            //         IconButton{
            //             implicitWidth: 30
            //             implicitHeight: 20
            //             icon.name: FluentIcons.graph_Search
            //             enabled: false
            //             icon.width: 14
            //             icon.height: 14
            //         }
            //     }
            //     onTap:
            //         (item)=>{
            //             if(item.key){
            //                 page_router.go(item.key)
            //             }
            //         }
            //     Connections{
            //         target: navigation_view
            //         function onSourceItemsChanged(data){
            //             auto_suggset_search.items = data.filter((item)=>{ return item instanceof PaneItem})
            //         }
            //     }
            // }
            trailing: Item{
                height: 40
                Label{
                    text: qsTr("Custom Content")
                    anchors.centerIn: parent
                }
            }
            onTap:
                (item)=>{
                    if(item.key){
                        page_router.go(item.key)
                    }
                }
            onRightTap:
                (item)=>{
                    if(item.key){
                        item_menu.showMenu(item)
                    }
                }
            // Component.onCompleted: {
            //     var homeId = function(){return navigation_view.sideBarView.itemAtIndex(0)}
            //     // window.tourSteps.push({title:qsTr("Home"),description: qsTr("Here you can switch to Home."),target:homeId,isLast: true})
            //     page_router.go("/")
            // }


            Component.onCompleted: {
                page_router.go(Global.displayMainScreen,{title:"Satrt"})
            }

            Connections{
                target:Global
                function onDisplayMainScreenChanged(){
                    page_router.go(Global.displayMainScreen)
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

        // 左侧按钮，从左到右排列
        RowLayout {
            id:foot_bar_left

            anchors
            {
                left:parent.left
                leftMargin:0
                verticalCenter: parent.verticalCenter
            }

            Layout.fillWidth: true  // 左侧区域填充可用空间
            spacing: 5

            IconButton{
                id: connect_state

                text: qsTr("未知状态")
                icon.name: FluentIcons.graph_HardDrive
                icon.width: 18
                icon.height: 18
                spacing: 5
                display: IconButton.IconOnly
                icon.color:"Grey"
                // anchors.verticalCenter: parent.verticalCenter

                property int iter:0
                onClicked: {

                    iter=iter%3;
                    switch (iter) {
                    case 0:
                        icon.name=FluentIcons.graph_DisconnectDrive
                        text= qsTr("已断开")
                        break;
                    case 1:
                        icon.name=FluentIcons.graph_MapDrive
                        text= qsTr("已连接")
                        break;
                    case 2:
                        icon.name=FluentIcons.graph_ResetDrive
                        text= qsTr("已重置")
                        break;
                    default:
                        console.log("Default case");
                    }
                    iter+=1
                }

                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: text
            }

            Text{
                text: qsTr("连接状态：")+connect_state.text
                opacity: 0.3
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

                onClicked:{
                    Global.visable_right_side=!Global.visable_right_side
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


    //应用顶部：左侧按钮，从左到右排列



}
