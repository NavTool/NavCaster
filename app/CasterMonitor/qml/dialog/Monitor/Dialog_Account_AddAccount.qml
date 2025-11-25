import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Controls
import FluentUI.Controls
import FluentUI.impl
import CasterMonitor

Item {
    property string title
    property PageContext context
    property var argument


    id:root


    property int account_type:0   // 0 永久  1：期限（天数）  2：期限（日期） 3：时限


    Component.onCompleted: {
        contentDialog.open()
    }

    Dialog {
        id: contentDialog

        x: Math.ceil((parent.width - width) / 2)
        y: Math.ceil((parent.height - height) / 2)
        parent: Overlay.overlay
        closePolicy: Popup.NoAutoClose //设置不自动关闭，如果设置了点击空白处这个对话框就会关闭
        modal: true
        title: qsTr("添加账号")
        standardButtons: Dialog.Yes | Dialog.No

        width: 600
        contentHeight: 500
        Frame {
            anchors.fill: parent

            Column {

                topPadding: 20
                anchors.horizontalCenter: parent.horizontalCenter

                spacing: 10


                Row {
                    Label {
                        text: qsTr("用户信息")
                        font: Typography.subtitle
                    }
                }

                Row {
                    spacing: 10
                    Label {
                        text: qsTr("用户名/机构名:")
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    TextBox {
                        width: 400
                    }
                }

                Row {
                    spacing: 10
                    Label {
                        text: qsTr("联系人:")
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    TextBox {
                        width: 185
                    }
                    Label {
                        text: qsTr("联系方式:")
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    TextBox {
                        width: 185
                    }
                }

                Row {
                    Label {
                        text: qsTr("账号配置")
                        font: Typography.subtitle
                    }
                }

                Row {
                    spacing: 10
                    Label {
                        text: "账号:"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    TextBox {
                        width: 450
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {
                    spacing: 10

                    Label {
                        text: "密码:"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    PasswordBox {
                        width: 400
                        placeholderText: "TextField"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Button {
                        text: "生成"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {
                    spacing: 10
                    Label {
                        text: "最大支持连接数:"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    TextBox {
                        width: 100
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Item {
                        height: 1
                        width: 50
                    }

                    Label {
                        text: "接入类型:"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    CheckBox {
                        text: "基准站"
                    }
                    CheckBox {
                        text: "移动站"
                    }
                }

                Row {
                    spacing: 10
                    Label {
                        text: qsTr("访问权限:")
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    MultiSelectComboBox{
                        width: 420
                        anchors.verticalCenter: parent.verticalCenter
                        model: ["完全访问", "XX机构专用", "内部测试", "最近点模式", "代理模式"]
                        FluentUI.minimumHeight: 240
                        Component.onCompleted: {
                            visualModel.items.get(0).inSelected = true
                        }
                    }
                }

                Row {
                    Label {
                        text: qsTr("有效期配置")
                        font: Typography.subtitle
                    }
                }

                Row {
                    spacing: 10
                    Label {
                        text: qsTr("账号类型:")
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    ComboBox {
                        width: 335
                        model: ["永久账号", "期限账号(天数)", "期限账号(日期)", "时限账号"]
                        anchors.verticalCenter: parent.verticalCenter

                        onCurrentIndexChanged:
                        {
                           root.account_type=currentIndex
                        }

                    }
                    CheckBox {
                        text: "立即激活"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                Row {
                    spacing: 10

                    visible: root.account_type===1

                    Label {
                        text: qsTr("有效天数:")
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    ComboBox {

                        width:150

                        model: ListModel {
                            id: model
                            ListElement {
                                text: "1"
                            }
                            ListElement {
                                text: "3"
                            }
                            ListElement {
                                text: "7"
                            }
                            ListElement {
                                text: "14"
                            }
                            ListElement {
                                text: "30"
                            }
                            ListElement {
                                text: "90"
                            }
                            ListElement {
                                text: "180"
                            }
                            ListElement {
                                text: "365"
                            }
                        }
                        editable: true
                        onAccepted: {
                            if (find(editText) === -1)
                                model.append({
                                                 "text": editText
                                             })
                        }
                    }
                    Label {
                        text: "天   "
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Item {
                        height: 1
                        width: 10
                    }
                    Label {
                        text: "失效时间:"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    CalendarPicker {
                        showTime: true
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {
                    visible: root.account_type===2

                    spacing: 10
                    Label {
                        text: "过期时间:"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    CalendarPicker {
                        showTime: true
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {

                    visible: root.account_type===3

                    spacing: 10
                    Label {
                        text: "可用时长:"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    NumberBox {
                        width:150
                        value: 10
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Label {
                        text: "小时"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Item {
                        height: 1
                        width: 10
                    }
                    Label {
                        text: "失效时间:"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    CalendarPicker {
                        showTime: true
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }



            }
        }
    }
}
// Item{
//     property string title
//     property PageContext context
//     property var argument

//     Dialog{
//         id:contentDialog

//         x: Math.ceil((parent.width - width) / 2)
//         y: Math.ceil((parent.height - height) / 2)
//         parent: Overlay.overlay
//         closePolicy: Popup.NoAutoClose //设置不自动关闭，如果设置了点击空白处这个对话框就会关闭
//         modal: true
//         title: qsTr("未设置对话框标题")
//         standardButtons: Dialog.Yes | Dialog.No

//         width: 600
//         contentHeight: 300
//         Frame{
//             anchors.fill: parent

//             Column{
//                 anchors.centerIn: parent
//                 spacing: 10
//                 Label{
//                     text: " xxx < 闭合环边数 <  xxx"
//                 }
//             }

//         }
//         Component.onCompleted:
//         {
//             open()
//         }
//     }
// }

