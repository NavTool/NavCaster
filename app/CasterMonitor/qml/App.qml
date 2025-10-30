import QtQuick
import QtQuick.Controls
import FluentUI.Controls
import FluentUI.impl

Starter {
    id: starter
    Component.onCompleted: {
        WindowRouter.routes = {
            "/": R.resolvedUrl("/qml/window/MainWindow.qml")
        }
        WindowRouter.go("/")
    }
}
