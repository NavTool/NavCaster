import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import FluentUI.Controls
import FluentUI.impl
import CasterMonitor
ScrollablePage{



    property string focusItemUID: ""   // 当前选定的数据记录的UID
    property var focusItem          // 选定记录的详细数据
    property var refreshDataOpUid      // 数据刷新操作的UID 重复调用这个UID指向的任务来刷新数据

    Component.onCompleted: {
        //创建刷新数据操作
        refreshDataOpUid = CasterMonitor.addRefreshNodeOperate()
        console.log("refreshDataOpUid: ", refreshDataOpUid)
        // 执行这个指令
        CasterMonitor.excuteOperate(refreshDataOpUid)
        // 启动定时器，定期刷新数据
        data_refresh_timer.start()
    }

    DataGridModel {
        id: dataModel
    }

    Timer {
        id: data_refresh_timer
        repeat: true
        interval: 1000
        onTriggered: {
            CasterMonitor.excuteOperate(refreshDataOpUid)
        }
    }

    Connections {
        target: CasterMonitor

        function onOperateFinished(taskID, success, info) {
            if (taskID !== refreshDataOpUid) {
                return  //非当前指令,跳过
            }
            // 执行数据刷新操作

            // console.log("onOperateFinished: " +taskID)

            CasterResourceController.updateNodeData()
        }
    }

    Connections{
        target: CasterResourceController

        function onUpdateNodeDataSuccess()
        {
            // 更新数据源
            dataModel.sourceData = CasterResourceController.node_status_data
        }
    }


    Column
    {

        spacing: 10
        Row{

            spacing: 10


            Frame
            {
                width: 300
                height: 180


                Label{
                    text: qsTr("负载状态\nCPU/节点数")
                    anchors.centerIn: parent
                    font: Typography.subtitle
                }
            }

            Frame
            {
                width: 300
                height: 180

                Label{
                    text: qsTr("在线基站/基站上限")
                    anchors.centerIn: parent
                    font: Typography.subtitle
                }

            }
            Frame
            {
                width: 300
                height: 180

                Label{
                    text: qsTr("在线移动站/移动站上限")
                    anchors.centerIn: parent
                    font: Typography.subtitle
                }

            }

            Frame
            {
                width: 300
                height: 180


                Label{
                    text: qsTr("集群状态\n运行中/未启动/离线")
                    anchors.centerIn: parent
                    font: Typography.subtitle
                }
            }

        }

        Frame
        {
            width: parent.width
            height: 140

            Row{
                anchors.verticalCenter: parent.verticalCenter

                leftPadding: 30
                spacing: 30

                Frame
                {
                    width: 200
                    height: 100
                    Label{
                        text: qsTr("用户数\n  104/856")
                        anchors.centerIn: parent
                        font: Typography.subtitle
                    }
                }

                Frame
                {
                    width: 200
                    height: 100
                    Label{
                        text: qsTr("异常事件\n  13")
                        anchors.centerIn: parent
                        font: Typography.subtitle
                    }
                }

                Frame
                {
                    width: 200
                    height: 100
                    Label{
                        text: qsTr("数据接入\n  104/856")
                        anchors.centerIn: parent
                        font: Typography.subtitle
                    }
                }
                Frame
                {
                    width: 200
                    height: 100
                    Label{
                        text: qsTr("数据推送\n  104/856")
                        anchors.centerIn: parent
                        font: Typography.subtitle
                    }
                }
            }
        }

    }


    Label{
        text: qsTr("节点状态")
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
        model: dataModel
        interactive: false
        delegate: com_item
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

                Column
                {
                    Label{
                        text: "节点ID: "+ model.UID
                    }
                    Label{
                        text: "节点版本: "+ model.tag_version
                    }
                    Label{
                        text: "CPU负载: "+ model.cpu_usage.toFixed(2) + "%"
                    }
                    Label{
                        text: "内存占用: "+formatBytes(model.mem_usage)
                    }
                }

                onClicked: {
                    // context.router.go(model.url)
                }
            }
        }
    }


    function formatBytes(bytes) {
        if (bytes === 0)
            return "0 B"
        var k = 1024
        var sizes = ["Byte", "KB", "MB", "GB", "TB"]
        var i = Math.floor(Math.log(bytes) / Math.log(k))
        var value = bytes / Math.pow(k, i)
        return value.toFixed(3) + " " + sizes[i]
    }



}

