import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import FluentUI.Controls
import FluentUI.impl
import CasterMonitor
import "../extra"


Item{

    property string title
    property PageContext context

    property list<QtObject> originalItems : [
        PaneItem{
            key: "/init/page/home"
            title: qsTr("主页")
            icon.name: FluentIcons.graph_Home
            icon.color:  Theme.res.textFillColorPrimary
        },
        PaneItem{
            icon.name: FluentIcons.graph_Settings
            icon.color:  Theme.res.textFillColorPrimary
            key: "/init/page/setting"
            title: qsTr("设置")
        }

    ]
    property list<QtObject> originalFooterItems : [
        PaneItem{
            icon.name: FluentIcons.graph_Info
            icon.color:  Theme.res.textFillColorPrimary
            key: "/init/page/about"
            title: qsTr("软件信息")
        }
    ]
    PageRouter{
        id: page_router
        routes: {
            "/init/page/home":{url: R.resolvedUrl("qml/page/Init/Page_Home.qml"),singleton:true},
            "/init/page/setting":{url: R.resolvedUrl("qml/page/Init/Page_Setting.qml"),singleton:true},
            "/init/page/about":{url: R.resolvedUrl("qml/page/Init/Page_About.qml"),singleton:true}
        }
    }

    NavigationView{
        anchors.fill: parent
        logo: Global.windowIcon
        title: Global.windowName +" v."+ PROJECT_SET_VERSION
        router: page_router
        items: originalItems
        footerItems: originalFooterItems
        displayMode: NavigationViewType.Top
        sideBarShadow: false
        // sideItemHeight: 85
        // sideBarWidth: 200
        appBarHeight: 48
        goBackButton.visible:false
        logoDelegate: comp_logo


        onTap:
            (item)=>{
                if(item.key){
                    page_router.go(item.key,{info:item.title})
                }
            }

        Component.onCompleted: {
            page_router.go(Global.displayInitScreen,{title:"Satrt"})
        }

        Connections{
            target:Global
            function onDisplayInitScreenChanged(){
                page_router.go(Global.displayInitScreen)
            }
        }

        Component{
            id: comp_logo
            Image{
                width: Global.windowIcon ? 25 : 0
                height: width
                source: Global.windowIcon ? Global.windowIcon : ""
            }
        }

    }
}
