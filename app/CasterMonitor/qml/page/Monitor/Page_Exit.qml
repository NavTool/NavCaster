import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import FluentUI.Controls
import FluentUI.impl
import CasterMonitor
ScrollablePage{

    title: qsTr("Exit")



    Component.onCompleted:
    {
        contentDialog.open()
    }


    Dialog{
        id:contentDialog

        x: Math.ceil((parent.width - width) / 2)
        y: Math.ceil((parent.height - height) / 2)
        parent: Overlay.overlay
        closePolicy: Popup.NoAutoClose //设置不自动关闭，如果设置了点击空白处这个对话框就会关闭
        modal: true
        title: qsTr("")
        standardButtons: Dialog.Yes | Dialog.No

        width: 600
        contentHeight: 300
        Frame{
            anchors.fill: parent

            Column{
                anchors.centerIn: parent
                spacing: 10
                Label{
                    text: " xxx < 闭合环边数 <  xxx"
                }
            }

        }
        Component.onCompleted:
        {
            // open()
        }
    }
}

