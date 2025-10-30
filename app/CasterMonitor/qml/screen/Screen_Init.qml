import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import FluentUI.Controls

Item{
    Column{
        anchors.centerIn: parent
        spacing: 15
        Image{
            width: 60
            height: 60
            source: "qrc:/qt/qml/CasterMonitor/res/logo.png"
            anchors.horizontalCenter: parent.horizontalCenter
        }
        Label{
            text: qsTr("CasterMonitor")
            anchors.horizontalCenter: parent.horizontalCenter
            font: Typography.title
        }
    }
}
