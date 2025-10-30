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
        },
        PaneItem{
            key: "/gnss/page/resource/baseline"
            title: "动态基线"
            icon.name: FluentIcons.graph_MarketDown
            icon.color:  Theme.res.textFillColorPrimary
        },
        PaneItem{
            key: "/gnss/page/resource/closeloop"
            title: "闭合环"
            icon.name: FluentIcons.graph_Eject
            icon.color:  Theme.res.textFillColorPrimary
        },
        PaneItem{
            key: "/gnss/page/resource/quality"
            title: "数据质量"
            icon.name: FluentIcons.graph_Trackers
            icon.color:  Theme.res.textFillColorPrimary
        },
        PaneItem{
            key: "/gnss/page/resource/solution"
            title: "解算结果"
            icon.name: FluentIcons.graph_BulletedList
            icon.color:  Theme.res.textFillColorPrimary
        },
        PaneItem{
            key: "/gnss/page/resource/navfile"
            title: "星历文件"
            icon.name: FluentIcons.graph_Globe
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
    NavigationViewEx{
        anchors.fill: parent
        //logo: "qrc:/qt/qml/Gallery/res/image/logo.png"
        //title: "FluentUI Gallery"
        router: page_router
        items: originalItems
        footerItems: originalFooterItems
        displayMode: NavigationViewType.Compact
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
            page_router.go(GNSS.displayResourcePage)
        }

        Connections{
            target:GNSS
            function onDisplayResourcePageChanged(){
                page_router.go(GNSS.displayResourcePage)
            }
        }

    }
}
