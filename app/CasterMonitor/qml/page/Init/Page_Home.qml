import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import FluentUI.Controls
import FluentUI.impl
import CasterMonitor

ScrollablePage {
    topPadding: 0
    leftPadding: 0
    rightPadding: 0
    background: Item{}
    Item{
        Layout.fillWidth: true
        Layout.preferredHeight: 340
        Image {
            fillMode:Image.PreserveAspectCrop
            width: parent.width
            height: 320
            verticalAlignment: Qt.AlignTop
            sourceSize: Qt.size(960,640)
            source: "qrc:/qt/qml/CasterMonitor/res/bg_home_header.webp"
            Rectangle{
                anchors.fill: parent
                gradient: Gradient{
                    GradientStop { position: 0.8; color: Theme.dark ? Qt.rgba(0,0,0,0) : Qt.rgba(1,1,1,0) }
                    GradientStop { position: 1.0; color: Theme.dark ? Qt.rgba(0,0,0,1) : Qt.rgba(1,1,1,1) }
                }
            }
            Label{
                text: "Caster Monitor"
                font: Typography.titleLarge
                anchors{
                    top: parent.top
                    left: parent.left
                    topMargin: 40
                    leftMargin: 20
                }
            }
        }
        ListView{
            anchors{
                left: parent.left
                right: parent.right
                bottom: parent.bottom
                leftMargin: 20
                rightMargin: 20
            }
            orientation: ListView.Horizontal
            height: 200
            boundsBehavior: Flickable.StopAtBounds
            model:ListModel{
                ListElement{
                    icon: "qrc:/qt/qml/CasterMonitor/res/image/AppBarToggleButton.png"
                    title: qsTr("创建连接")
                    desc: qsTr("连接到Caster实例/集群")
                    url: "https://zhuzichu520.github.io/"
                    clicked: function(){
                        Global.displayInitScreen="/init/page/start"
                    }
                }
            }
            spacing: 20
            delegate: Rectangle{
                width: 200
                height: 200
                color: Theme.res.micaBackgroundColor
                radius: 8
                StandardButton{
                    id: item_btn_header
                    anchors.fill: parent
                    FluentUI.radius: parent.radius
                    onClicked: {
                        model.clicked(model)
                    }
                    ColumnLayout{
                        Image {
                            Layout.topMargin: 20
                            Layout.leftMargin: 20
                            Layout.preferredWidth: 50
                            Layout.preferredHeight: 50
                            source: model.icon
                        }
                        Label{
                            text: model.title
                            Layout.topMargin: 16
                            Layout.leftMargin: 20
                            font: Typography.bodyStrong
                            color: item_btn_header.FluentUI.textColor
                        }
                        Label{
                            text: model.desc
                            Layout.topMargin: 5
                            Layout.preferredWidth: 160
                            Layout.leftMargin: 20
                            wrapMode: Text.WrapAnywhere
                            font: Typography.caption
                            color: Theme.res.textFillColorTertiary
                        }
                    }
                    Icon{
                        source: FluentIcons.graph_OpenInNewWindow
                        width: 15
                        height: 15
                        anchors{
                            bottom: parent.bottom
                            right: parent.right
                            rightMargin: 10
                            bottomMargin: 10
                        }
                    }
                }
            }
        }
    }


    Component{
        id:com_item
        Item{
            width: 320
            height: 120
            StandardButton{
                FluentUI.radius: 8
                width: 300
                height: 100
                anchors.centerIn: parent
                onClicked: {
                    context.router.go(model.url)
                }
                Image{
                    id:item_icon
                    height: 40
                    width: 40
                    source: model.image
                    anchors{
                        left: parent.left
                        leftMargin: 20
                        verticalCenter: parent.verticalCenter
                    }
                }
                Label{
                    id:item_title
                    text: model.title
                    font: Typography.bodyStrong
                    color: parent.FluentUI.textColor
                    anchors{
                        left: item_icon.right
                        leftMargin: 20
                        top: item_icon.top
                    }
                }
                Label{
                    id:item_desc
                    text: model.desc
                    color: Colors.grey120
                    wrapMode: Text.WrapAnywhere
                    elide: Text.ElideRight
                    font: Typography.caption
                    maximumLineCount: 2
                    anchors{
                        left: item_title.left
                        right: parent.right
                        rightMargin: 20
                        top: item_title.bottom
                        topMargin: 5
                    }
                }
                Rectangle{
                    height: 12
                    width: 12
                    radius:  6
                    color: Theme.accentColor.defaultBrushFor()
                    anchors{
                        right: parent.right
                        top: parent.top
                        rightMargin: 14
                        topMargin: 14
                    }
                }
            }
        }
    }

    ListModel{
        id: model_rencently_added
        ListElement{
            title: qsTr("Caster Test")
            desc: qsTr("81.68.72.44:16379")
            image: "qrc:/qt/qml/CasterMonitor/res/image/AnimatedIcon.png"
            url: "/navigation/tabview"
        }
    }

    Label{
        text: qsTr("最近使用的连接")
        font: Typography.title
        Layout.topMargin: 20
        Layout.leftMargin: 20
    }

    GridView{
        Layout.fillWidth: true
        Layout.preferredHeight: contentHeight
        Layout.leftMargin: 10
        Layout.rightMargin: 10
        cellHeight: 120
        cellWidth: 320
        model: model_rencently_added
        interactive: false
        delegate: com_item
    }

    ListModel{
        id: model_rencently_updated
        ListElement{
            title: qsTr("Caster Test")
            desc: qsTr("81.68.72.44:16379")
            image: "qrc:/qt/qml/CasterMonitor/res/image/StandardUICommand.png"
            url: "/form/autosuggestbox"
        }
    }

    Label{
        text: qsTr("已保存的连接")
        font: Typography.title
        Layout.topMargin: 20
        Layout.leftMargin: 20
    }

    GridView{
        Layout.fillWidth: true
        Layout.preferredHeight: contentHeight
        Layout.leftMargin: 10
        Layout.rightMargin: 10
        cellHeight: 120
        cellWidth: 320
        model: model_rencently_updated
        interactive: false
        delegate: com_item
    }

}
