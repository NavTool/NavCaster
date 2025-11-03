import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Qt.labs.qmlmodels
import FluentUI.Controls
import FluentUI.impl
import CasterMonitor

ScrollablePage{

    title: qsTr("Settings")
    columnSpacing: 24

    Dialog {
        id: dialog_restart
        x: Math.ceil((parent.width - width) / 2)
        y: Math.ceil((parent.height - height) / 2)
        parent: Overlay.overlay
        modal: true
        width: 300
        title: qsTr("Friendly Reminder")
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: {
            WindowRouter.exit(931)
        }

        Column {
            spacing: 20
            anchors.fill: parent
            Label {
                width: parent.width
                wrapMode: Label.WrapAnywhere
                text: qsTr("This action requires a restart of the program to take effect, is it restarted?")
            }
        }
    }
    GroupBox{
        title: qsTr("General")
        Layout.fillWidth: true
        ColumnLayout {
            anchors.fill: parent
            CheckBox{
                text: qsTr("Adapt to PC Display Scaling")
                checked: SettingsHelper.getSystemScalingAdjusted()
                checkable: false
                onClicked: {
                    checked = !checked
                    SettingsHelper.saveSystemScalingAdjusted(checked)
                    dialog_restart.open()
                }
            }
        }
    }
    GroupBox{
        title: qsTr("Theme mode")
        Layout.fillWidth: true
        ColumnLayout {
            anchors.fill: parent
            RadioButton {
                checked: Theme.darkMode === FluentUI.System
                text: qsTr("system")
                onClicked: {
                    Theme.darkMode = FluentUI.System
                }
            }
            RadioButton {
                checked: Theme.darkMode === FluentUI.Light
                text: qsTr("light")
                onClicked: {
                    Theme.darkMode = FluentUI.Light
                }
            }
            RadioButton {
                checked: Theme.darkMode === FluentUI.Dark
                text: qsTr("dark")
                onClicked: {
                    Theme.darkMode = FluentUI.Dark
                }
            }
        }
    }
    GroupBox{
        title: qsTr("Window background effect (works only on Windows 11)")
        Layout.fillWidth: true
        ColumnLayout {
            anchors.fill: parent
            Button{
                text: window.visibility === Window.FullScreen ? qsTr("Windowed") : qsTr("FullScreen")
                onClicked: {
                    if(window.visibility === Window.FullScreen){
                        window.showNormal()
                    }else{
                        window.showFullScreen()
                    }
                }
            }
            RadioButton {
                checked: Global.windowEffect === WindowEffectType.Normal
                text: qsTr("normal")
                onClicked: {
                    Global.windowEffect = WindowEffectType.Normal
                }
            }
            RadioButton {
                checked: Global.windowEffect === WindowEffectType.Mica
                text: qsTr("mica")
                onClicked: {
                    Global.windowEffect = WindowEffectType.Mica
                }
            }
            RadioButton {
                checked: Global.windowEffect === WindowEffectType.Acrylic
                text: qsTr("acrylic")
                onClicked: {
                    Global.windowEffect = WindowEffectType.Acrylic
                }
            }
            RadioButton {
                checked: Global.windowEffect === WindowEffectType.MicaAlt
                text: qsTr("mica alt")
                onClicked: {
                    Global.windowEffect = WindowEffectType.MicaAlt
                }
            }
        }
    }

    ListModel{
        id: accentColors
        ListElement{
            name: "Yellow"
            color: function(){return Colors.yellow}
        }
        ListElement{
            name: "Orange"
            color: function(){return Colors.orange}
        }
        ListElement{
            name: "Red"
            color: function(){return Colors.red}
        }
        ListElement{
            name: "Magenta"
            color: function(){return Colors.magenta}
        }
        ListElement{
            name: "Purple"
            color: function(){return Colors.purple}
        }
        ListElement{
            name: "Blue"
            color: function(){return Colors.blue}
        }
        ListElement{
            name: "Teal"
            color: function(){return Colors.teal}
        }
        ListElement{
            name: "Green"
            color: function(){return Colors.green}
        }
    }

    GroupBox{
        title: qsTr("Primary Color")
        Layout.fillWidth: true
        Flow {
            spacing: 10
            anchors.fill: parent
            Repeater{
                model: accentColors
                delegate: Rectangle{
                    required property var model
                    implicitWidth: 48
                    implicitHeight: 48
                    radius: 4
                    border.color: model.color().darkest()
                    border.width: 1
                    color: mouse_item_accent_color.containsMouse? model.color().lightest() : model.color().normal
                    Icon{
                        source: FluentIcons.graph_CheckMark
                        width: 24
                        height: 24
                        anchors.centerIn: parent
                        color: Colors.basedOnLuminance(parent.color)
                        visible: model.color() === Theme.primaryColor
                    }
                    MouseArea{
                        id: mouse_item_accent_color
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            Theme.primaryColor = model.color()
                        }
                    }
                }
            }
        }
    }

    GroupBox{
        title: qsTr("Locale")
        Layout.fillWidth: true
        Flow {
            spacing: 10
            anchors.fill: parent
            Repeater{
                model: AppInfo.locales
                delegate: RadioButton{
                    text: modelData
                    checked: AppInfo.locale === modelData
                    onClicked: {
                        AppInfo.locale = modelData
                        SettingsHelper.saveLocale(modelData)
                        dialog_restart.open()
                    }
                }
            }
        }
    }

    GroupBox{
        title: qsTr("Auout")
        spacing: 6
        padding: 0
        Layout.fillWidth: true

        // Frame{
        //     anchors.fill:parent

        Column{
            anchors.fill:parent

            Button{
                width:parent.width
                implicitHeight:68

                onClicked:{
                    about_info.visible=!about_info.visible
                }

                Row{
                    anchors{
                        verticalCenter: parent.verticalCenter
                        left:parent.left
                        leftMargin:30
                    }

                    spacing:20

                    Image{
                        anchors.verticalCenter: parent.verticalCenter
                        source: Global.windowIcon
                        width: 40
                        height: width
                        // fillMode: Image.PreserveAspectFit
                    }
                    Column{

                        anchors.verticalCenter: parent.verticalCenter

                        Text{
                            text:PROJECT_NAME
                            font:Typography.bodyStrong
                            color:Theme.res.textFillColorPrimary
                        }
                        Text{
                            text:SUPPORT_COPYRIGHT
                            font:Typography.caption
                            color:Theme.res.textFillColorSecondary
                        }
                    }
                }

                Icon{
                    anchors{
                        right:parent.right
                        rightMargin:30
                        verticalCenter: parent.verticalCenter
                    }

                    source:about_info.visible?FluentIcons.graph_ChevronUp:FluentIcons.graph_ChevronDown
                }
            }

            Frame{
                id:about_info
                width:parent.width
                implicitHeight:400

                visible:false

                TableView{
                    anchors.fill: parent
                    topMargin: 15
                    columnSpacing: 0
                    rowSpacing: 5

                    interactive:false // 设置交互为false则禁止拖动

                    model:TableModel{
                        TableModelColumn{display: "key"}
                        TableModelColumn{display:"value"}

                        rows:[
                            {
                                key:"软件名称",
                                value:PROJECT_NAME
                            },
                            {
                                key:"版本",
                                value:PROJECT_GIT_BRANCH
                            },
                            {
                                key:"版本号",
                                value:PROJECT_SET_VERSION
                            },
                            {
                                key:"GIT版本",
                                value:PROJECT_GIT_VERSION
                            },
                            {
                                key:"TAG版本",
                                value:PROJECT_TAG_VERSION
                            },
                            {
                                key:"提交日期",
                                value:PORJECT_UPDATE_TIME
                            },
                            {
                                key:"编译环境",
                                value:BUILD_SYSTEM
                            },
                            {
                                key:"编译架构",
                                value:BUILD_SYSTEM_PROCESSOR
                            },
                            {
                                key:"编译器版本",
                                value:BUILD_COMPILER_VERSION
                            },
                            {
                                key:"构建日期",
                                value:BUILD_DATE
                            },
                            {
                                key:"激活策略",
                                value:"内测版本"
                            },
                            {
                                key:"激活状态",
                                value:"无需激活"
                            },
                            {
                                key:"过期时间",
                                value:"0000/0/0"
                            },
                            {
                                key:"设备码",
                                value:"1c99a3cc-d75e-4da9-8e94-00d89a7dffde"
                            },
                            {
                                key:"激活密钥",
                                value:"1c99a3cc-d75e-4da9-8e94-00d89a7dffde"
                            }
                        ]
                    }

                    delegate: DelegateChooser{
                        DelegateChoice{
                            column: 0
                            delegate:Item {
                                implicitWidth:160
                                implicitHeight:20
                                Label{
                                    text:model.display
                                    font: Typography.bodyStrong
                                    color: Theme.res.textFillColorPrimary
                                    anchors{
                                        //horizontalCenter: parent.horizontalCenter
                                        verticalCenter: parent.verticalCenter
                                        left: parent.left
                                        leftMargin: 100
                                    }
                                }
                            }
                        }
                        DelegateChoice{
                            column: 1
                            delegate: Item {
                                implicitWidth:100
                                implicitHeight:20
                                CopyableText{
                                    text:model.display
                                    font: Typography.bodyStrong
                                    color: Theme.res.textFillColorSecondary
                                    anchors{
                                        //horizontalCenter: parent.horizontalCenter
                                        verticalCenter: parent.verticalCenter
                                        left: parent.left
                                        leftMargin: 100
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
