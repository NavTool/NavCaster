import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import FluentUI.Controls
import FluentUI.impl
import CasterMonitor

Item{

    id:root

    property var initialItem

    anchors.fill:parent

    InfoBarManager{
        id: tip_topright
        target: root
        edge: Qt.TopEdge | Qt.RightEdge
    }

    InfoBarManager{
        id: tip_top
        target: root
        edge: Qt.TopEdge
        isOverlay:true   //置顶，放置在最前端
    }

    InfoBarManager{
        id: tip_topleft
        target: root
        edge: Qt.TopEdge | Qt.LeftEdge
    }

    InfoBarManager{
        id: tip_bottomright
        target: root
        edge: Qt.BottomEdge | Qt.RightEdge
    }

    InfoBarManager{
        id: tip_bottom
        target: root
        edge: Qt.BottomEdge
    }

    InfoBarManager{
        id: tip_bottomleft
        target: root
        edge: Qt.BottomEdge | Qt.LeftEdge
    }

    PageRouter{
        id: screen_router
        routes: {
            "/screen/init": R.resolvedUrl("qml/screen/Screen_Init.qml"),
            "/screen/main": R.resolvedUrl("qml/screen/Screen_Main.qml"),
        }
    }


    PageRouter{
        id: dialog_router
        routes: {
            "/monitor/dialog/account/add_account": R.resolvedUrl("qml/dialog/Monitor/Dialog_Account_AddAccount.qml"),
            "/monitor/dialog/alias/add_alias": R.resolvedUrl("qml/dialog/Monitor/Dialog_Alias_AddAlias.qml"),
            "/monitor/dialog/relay/add_pull": R.resolvedUrl("qml/dialog/Monitor/Dialog_Relay_AddPull.qml"),
            "/monitor/dialog/relay/add_push": R.resolvedUrl("qml/dialog/Monitor/Dialog_Relay_AddPush.qml")
        }
    }




    // 用于存储多个对话框的Loader
    HotLoader {
        id: dialogLoader

        // property string title
        // property var path
        // property var url
        // property var argument
        // property bool singleton
        // property var stackView: control
        // property var pageRouter: control.router
        // property alias __context: context
        // property var __current:{
        //     if(singleton){
        //         return url
        //     }
        //     return visible ? url : ""
        // }
        // on__CurrentChanged: {
        //     dialogLoader.setSource(__current,{context:context})
        // }
        // reload: function(){
        //     var timestamp = Date.now()
        //     var url = new URL(__current)
        //     url.searchParams.set('timestamp', timestamp);
        //     dialogLoader.setSource(url.toString(),{context:context})
        // }
        // StackView.onActivated: {
        //     context.activated()
        // }
        // StackView.onDeactivated: {
        //     context.deactivated()
        // }
        // PageContext{
        //     id: context
        //     path: dialogLoader.path
        //     url: dialogLoader.url
        //     argument: dialogLoader.argument
        //     singleton: dialogLoader.singleton
        //     view: dialogLoader.stackView
        //     router: dialogLoader.pageRouter
        // }
        // onLoaded: {
        //     dialogLoader.title = loader_panne.item.title
        // }

    }

    Connections{
        target: Global

        // function onOpen_dialog(path)
        // {
        //     if(dialogLoader.source===dialog_router.toUrl(path))
        //     {
        //         console.log("reload dialog")
        //         dialogLoader.reload()
        //     }
        //     else
        //     {
        //         console.log("open dialog:"+path)
        //         dialogLoader.source = dialog_router.toUrl(path)
        //         dialogLoader.reload()
        //     }
        // }


        function onOpen_dialog(path, args) {
            let url = dialog_router.toUrl(path)
            if (dialogLoader.source === url) {
                console.log("reload dialog with args")
                dialogLoader.setSource(url, {argument: args})
            } else {
                console.log("open dialog: " + path)
                dialogLoader.setSource(url, {argument: args})
            }
        }
    }



    PageRouterView{
        id: screen_panne
        anchors.fill: parent
        router: screen_router
        clip: true

        Component.onCompleted: {
            screen_router.go(Global.displayScreen)
        }
    }

    Connections{
        target:Global
        function onDisplayScreenChanged(){
            screen_router.go(Global.displayScreen)
        }
    }
    Shortcut {
        sequence: "Ctrl+D"
        onActivated: {
            Global.debugMode = !Global.debugMode
            tip_top.showInfo(qsTr("Debug Mode:  %1").arg(Global.debugMode ? "Open" : "Close"))
        }
    }


    Connections
    {
        target: CasterMonitor

        function onNoticeSuccess(msg)
        {
            tip_top.showSuccess(msg)
        }
        function onNoticeInfo(msg)
        {
            tip_top.showInfo(msg)
        }
        function onNoticeWarning(msg)
        {
            tip_top.showWarning(msg)
        }
        function onNoticeError(msg)
        {
            tip_top.showError(msg)
        }

        function onConnectCasterSuccess()
        {
            tip_top.showSuccess("onConnectCasterSuccess")
        }

        function onConnectCasterFailed()
        {
            tip_top.showSuccess("onConnectCasterFailed")
        }

        function onReconnectCaster()
        {

        }

        function onDisconnectCaster()
        {

        }



    }





}

