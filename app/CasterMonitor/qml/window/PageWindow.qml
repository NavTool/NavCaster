import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import FluentUI.Controls
import FluentUI.impl
import CasterMonitor

FramelessWindow {
    property var argument
    property alias infoBarManager: infobar_manager
    id: window
    width: 1000
    height: 800
    minimumWidth: 500
    minimumHeight: 400
    visible: true
    windowEffect: Global.windowEffect
    onInit:
        (arg)=>{
            argument = arg
        }
    onNewInit:
        (arg)=>{
            argument = arg
        }
    initialItem: {
        if(argument){
            return argument.url
        }
        return undefined
    }
    appBar.windowIcon: Item{}
    InfoBarManager{
        id: infobar_manager
        target: window.contentItem
        messageMaximumWidth: 380
    }
}
