import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import FluentUI.Controls
import FluentUI.impl
import CasterMonitor

FramelessWindow {
    property var argument
    id: window
    title: "StandardWindow"
    width: 600
    height: 400
    fixSize: true
    visible: true
    launchMode: WindowType.Standard
    windowEffect: Global.windowEffect
    onInit:
        (arg)=>{
            argument = arg
        }
    onNewInit:
        (arg)=>{
            argument = arg
        }
    initialItem: R.resolvedUrl("res/qml/screen/MultiWindowScreen.qml")
}
