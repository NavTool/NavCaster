import QtQuick
import QtQuick.Controls
import FluentUI.Controls
import FluentUI.impl
import CasterMonitor
import "../../extra"

Frame{
    anchors.fill: parent

    property string title
    property PageContext context


    property list<QtObject> originalItems : [
        PaneItem{
            key: "/monitor/page/mpt/source"
            title: "源列表管理"
            icon.name: FluentIcons.graph_AllApps
            icon.color:  Theme.res.textFillColorPrimary
        },
        PaneItem{
            key: "/monitor/page/mpt/pull"
            title: "Ntrip数据接入"
            icon.name: FluentIcons.graph_ReturnToWindow
            icon.color:  Theme.res.textFillColorPrimary
        },
        PaneItem{
            key: "/monitor/page/mpt/push"
            title: "Ntrip数据推送"
            icon.name: FluentIcons.graph_OpenInNewWindow
            icon.color:  Theme.res.textFillColorPrimary
        },
        PaneItem{
            key: "/monitor/page/mpt/tcp"
            title: "TCP数据接入"
            icon.name: FluentIcons.graph_MapPin
            icon.color:  Theme.res.textFillColorPrimary
        },
        PaneItem{
            key: "/monitor/page/mpt/proxy"
            title: "Ntrip数据中继"
            icon.name: FluentIcons.graph_Relationship
            icon.color:  Theme.res.textFillColorPrimary
        },
        PaneItem{
            key: "/monitor/page/mpt/alias"
            title: "挂载点别名"
            icon.name: FluentIcons.graph_PrivateCall
            icon.color:  Theme.res.textFillColorPrimary
        },
        PaneItem{
            key: "/monitor/page/mpt/nearest"
            title: "最近挂载点"
            icon.name: FluentIcons.graph_InternetSharing
            icon.color:  Theme.res.textFillColorPrimary
        }

    ]
    property list<QtObject> originalFooterItems : [
        // PaneItem{
        //     icon.name: FluentIcons.graph_FavoriteList
        //     icon.color:  Theme.res.textFillColorPrimary
        //     key: "/gnss/page/resource/coord"
        //     title: qsTr("坐标(正式版需隐藏)")
        // },
        // PaneItem{
        //     icon.name: FluentIcons.graph_Settings
        //     icon.color:  Theme.res.textFillColorPrimary
        //     key: "/gnss/page/resource/option"
        //     title: qsTr("选项")
        // }
    ]
    PageRouter{
        id: page_router
        routes: {
            "/monitor/page/mpt/alias":{url: R.resolvedUrl("qml/page/Monitor/Page_Mpt_Alias.qml"),singleton:true},
            "/monitor/page/mpt/nearest":{url: R.resolvedUrl("qml/page/Monitor/Page_Mpt_Nearest.qml"),singleton:true},
            "/monitor/page/mpt/proxy":{url: R.resolvedUrl("qml/page/Monitor/Page_Mpt_Proxy.qml"),singleton:true},
            "/monitor/page/mpt/pull":{url: R.resolvedUrl("qml/page/Monitor/Page_Mpt_Pull.qml"),singleton:true},
            "/monitor/page/mpt/push":{url: R.resolvedUrl("qml/page/Monitor/Page_Mpt_Push.qml"),singleton:true},
            "/monitor/page/mpt/source":{url: R.resolvedUrl("qml/page/Monitor/Page_Mpt_Source.qml"),singleton:true},
            "/monitor/page/mpt/tcp":{url: R.resolvedUrl("qml/page/Monitor/Page_Mpt_TCP.qml"),singleton:true},
          }
    }
    NavigationView{
        anchors.fill: parent
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

    }
}
