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
            "/init/page/start":{url: R.resolvedUrl("qml/page/Init/Page_Start.qml"),singleton:true},
            "/init/page/home":{url: R.resolvedUrl("qml/page/Init/Page_Home.qml"),singleton:true},
            "/init/page/setting":{url: R.resolvedUrl("qml/page/Init/Page_Setting.qml"),singleton:true},
            "/init/page/about":{url: R.resolvedUrl("qml/page/Init/Page_About.qml"),singleton:true}
        }
    }


    PageRouterView{
        id: screen_panne
        anchors.fill: parent
        router: page_router
        clip: true

        Component.onCompleted: {
            page_router.go(Global.displayInitScreen,{title:"Satrt"})
        }

        Connections{
            target:Global
            function onDisplayInitScreenChanged(){
                page_router.go(Global.displayInitScreen)
            }
        }

    }



  }
