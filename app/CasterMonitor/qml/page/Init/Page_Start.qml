import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import FluentUI.Controls
import FluentUI.impl
import CasterMonitor

ContentPage {

    id:root

    property string login_ip: ""
    property int login_port:0
    property string login_auth:""



    property var colors : [Colors.yellow,Colors.orange,Colors.red,Colors.magenta,Colors.purple,Colors.blue,Colors.teal,Colors.green]

    property var randomAccentColor: function(){
        return colors[Math.floor(Math.random() * 8)]
    }

    topPadding: 0
    leftPadding: 0
    rightPadding: 0
    bottomPadding: 0
    background: Image {
        fillMode:Image.TileHorizontally
        width: parent.width
        height: parent.height*0.6
        anchors.bottom: parent.bottom       // 对齐底部
        verticalAlignment: Qt.AlignTop
        sourceSize: Qt.size(parent.width,parent.heigh)
        source: "qrc:/qt/qml/CasterMonitor/res/bg_home_header.webp"
        Rectangle{
            anchors.fill: parent
            gradient: Gradient{
                GradientStop { position: 0.7; color: Theme.dark ? Qt.rgba(0,0,0,0) : Qt.rgba(1,1,1,0) }
                GradientStop { position: 1.0; color: Theme.dark ? Qt.rgba(0,0,0,1) : Qt.rgba(1,1,1,1) }
            }
        }
    }


    Column{

        anchors{
            top: parent.top
            left: parent.left
            topMargin: 50
            leftMargin: 40
        }

        spacing: 10

        Item
        {
            width: 300
            height: 130
            Column{
                spacing: 10
                Image {
                    height: 80
                    width: 150
                    source: Global.windowIcon
                    fillMode: Image.PreserveAspectFit   // 保持比例
                }
                Label{
                    text: "Caster Monitor"
                    font: Typography.subtitle
                    anchors{
                        horizontalCenter: parent.horizontalCenter
                    }
                }
            }
        }

        Frame
        {
            clip: true

            width: 450
            height: 250
            BannerLayout {
                id: banner
                anchors.fill: parent
                orientation: Qt.Horizontal
                model: ListModel {
                    ListElement { picUrl: "qrc:/qt/qml/CasterMonitor/res/bg_home_header.webp" }
                    ListElement { picUrl: "qrc:/qt/qml/CasterMonitor/res/bg_home_header.webp" }
                    ListElement { picUrl: "qrc:/qt/qml/CasterMonitor/res/bg_home_header.webp" }
                }
                delegate: Item {
                    width: banner.width
                    height: banner.height
                    Image {
                        anchors.fill: parent
                        source: picUrl
                    }
                }
            }
            PageIndicator {
                anchors.bottom: banner.bottom
                anchors.horizontalCenter: banner.horizontalCenter
                count: banner.count
                currentIndex: banner.currentIndex
            }
        }

        Frame
        {
            width: 450
            height: 180

            ListModel{
                id: tab_model
                ListElement{
                    title: "First"
                    accentColor: function(){
                        return colors[Math.floor(Math.random() * 8)]
                    }
                }
                ListElement{
                    title: "Second"
                    accentColor: function(){
                        return colors[Math.floor(Math.random() * 8)]
                    }
                }
                ListElement{
                    title: "Third"
                    accentColor: function(){
                        return colors[Math.floor(Math.random() * 8)]
                    }
                }
            }

            SegmentedControl {

                width: parent.width

                id: bar
                clip: true
                Repeater {
                    model: tab_model
                    SegmentedButton {
                        id: btn_tab
                        text: model.title
                        width: 150
                    }
                }
            }

            Component{
                id:comp_page
                Frame{
                    anchors.fill: parent
                    Label{
                        font: Typography.titleLarge
                        anchors.centerIn: parent
                        text: modelData.title
                        color: modelData.accentColor().normal
                    }
                }
            }

            StackLayout {
                currentIndex: bar.currentIndex
                anchors{
                    left: bar.left
                    right: bar.right
                    top: bar.bottom
                    bottom: parent.bottom
                    topMargin: 10
                }
                Repeater{
                    model:tab_model
                    AutoLoader{
                        property var modelData: model
                        sourceComponent: comp_page
                    }
                }
            }
        }
    }

    Row
    {
        anchors{
            bottom: parent.bottom
            left: parent.left
            bottomMargin: 10
            leftMargin: 20
        }
        spacing: 0

        Item
        {
            width: 150
            height: 50
            Image {
                anchors.fill: parent
                source: Theme.dark ? Global.companyLogo_dark:Global.companyLogo_light
                fillMode: Image.PreserveAspectFit   // 保持比例
            }
        }
        Column
        {
            anchors.verticalCenter: parent.verticalCenter

            Label{
                text: PROJECT_SET_NAME + SUPPORT_COPYRIGHT
                font: Typography.bodyStrong
                color: "grey"
                anchors{
                    // horizontalCenter: parent.horizontalCenter
                }
            }
            Label{
                text: "软件版本："+Global.windowName  +" "+ PROJECT_TAG_VERSION + "      设备ID :" + VALUE_DEVICE_ID
                font: Typography.bodyStrong
                color: "grey"
                anchors{
                    // horizontalCenter: parent.horizontalCenter
                }
            }
        }
    }


    Column
    {
        anchors{
            top: parent.top
            right: parent.right
            topMargin: 100
            rightMargin:20
        }

        spacing: 30

        IconButton
        {
            icon.name: FluentIcons.graph_FavoriteList
            icon.color:  Theme.res.textFillColorSecondary

            // onClicked: {
            //     Global.displayInitScreen="/page/test"
            // }
        }
        IconButton
        {
            icon.name: FluentIcons.graph_HomeGroup
            icon.color:  Theme.res.textFillColorSecondary
        }
        IconButton
        {
            icon.name: FluentIcons.graph_Share
            icon.color:  Theme.res.textFillColorSecondary
        }
        IconButton
        {
            icon.name: FluentIcons.graph_Settings
            icon.color:  Theme.res.textFillColorSecondary
        }
    }


    Column{

        anchors{
            bottom: parent.bottom
            right: parent.right
            bottomMargin: 130
            rightMargin: 150
        }
        spacing: 10

        Acrylic{
            width: 300
            height: 160
            tintOpacity: 0.2
            blurRadius: 100

            Column
            {
                topPadding: 10
                spacing: 5
                Row{
                    spacing: 10
                    Item{
                        width: 50
                        height: 35
                        Label{
                            text: qsTr("IP :")
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.right: parent.right
                        }
                    }

                    TextBox{
                        width: 220
                        height: 35


                        onTextChanged:
                        {
                            root.login_ip=text
                        }
                        Component.onCompleted:
                        {
                            text="127.0.0.1"
                        }
                    }
                }
                Row{
                    spacing: 10
                    Item{
                        width: 50
                        height: 35
                        Label{
                            text: qsTr("Port :")
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.right: parent.right
                        }
                    }

                    TextBox{
                        width: 220
                        height: 35

                        onTextChanged:
                        {
                            root.login_port=text
                        }

                        Component.onCompleted:
                        {
                            text=16379
                        }

                    }
                }
                Row{
                    spacing: 10
                    Item{
                        width: 50
                        height: 35
                        Label{
                            text: qsTr("Auth:")
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.right: parent.right
                        }
                    }

                    PasswordBox{
                        width: 220
                        height: 35


                        onTextChanged:
                        {
                            root.login_auth=text
                        }

                    }
                }


                Row{
                    spacing: 40
                    leftPadding: 60
                    CheckBox{
                        text: qsTr("保存密码")

                    }

                    CheckBox{
                        text: qsTr("自动连接")

                    }
                }
            }
        }
        Frame{
            width: 300
            height:60

            Row{
                IconButton
                {
                    width: 240
                    height: 60

                    text: qsTr("连接")
                    font: Typography.title

                    onClicked:
                    {

                        //设置IP 端口 和密码

                        //调用接口

                        var info= {};

                        info.ip  = root.login_ip
                        info.port= root.login_port
                        info.auth=  root.login_auth

                        var task_uid= CasterMonitor.addConnectCasterOperate(info)
                        CasterMonitor.excuteOperate(task_uid)
                    }

                }
                IconButton
                {
                    width: 60
                    height: 60
                    icon.name: FluentIcons.graph_ChevronRightSmall
                    icon.color:  Theme.res.textFillColorSecondary

                    onClicked: {
                        Global.displayInitScreen="/init/page/home"
                    }
                }
            }


        }



    }



}
