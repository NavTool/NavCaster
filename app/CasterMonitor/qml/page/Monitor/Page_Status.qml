import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import FluentUI.Controls
import FluentUI.impl
import CasterMonitor
ScrollablePage{

    // topPadding: 0
    leftPadding: 0
    rightPadding: 0


    // title: qsTr("状态看板")

    property string focusItemUID: ""   // 当前选定的数据记录的UID
    property var focusItem          // 选定记录的详细数据
    property var refreshDataOpUid      // 数据刷新操作的UID 重复调用这个UID指向的任务来刷新数据


    Connections {
        target: Window.window
        onWidthChanged: console.log("Window width:", Window.window.width)
        onHeightChanged: console.log("Window height:", Window.window.height)
    }



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


            // cluster_model.clear()


            if(CasterResourceController.node_online===0)
            {
                return;
            }

            cluster_model.set(0,{key:qsTr("负载") ,
                                  value:formatUsage(CasterResourceController.cluster_cpu),
                                  percent:CasterResourceController.cluster_cpu })

            cluster_model.set(1,{key:qsTr("在线基站")  ,
                                  value:CasterResourceController.server_online.toString(),
                                  percent:CasterResourceController.server_online/100 })

            cluster_model.set(2,{key:qsTr("在线移动站") ,
                                  value:CasterResourceController.client_online.toString(),
                                  percent:CasterResourceController.client_online/100.0 })

            cluster_model.set(3,{key:qsTr("节点状态") ,
                                  value:CasterResourceController.node_online+ "/"+CasterResourceController.node_count,
                                  percent:CasterResourceController.node_online/CasterResourceController.node_count*100.0 })

            cluster_model.set(4,{key:qsTr("内存占用") ,
                                  value:formatBytes(CasterResourceController.cluster_mem),
                                  percent:0 })

            cluster_model.set(5,{key:qsTr("输入") ,
                                  value:formatMbps(CasterResourceController.cluster_recv_speed),
                                  percent:0 })

            cluster_model.set(6,{key:qsTr("输出") ,
                                  value:formatMbps(CasterResourceController.cluster_send_speed),
                                  percent:0 })

            cluster_model.set(7,{key:qsTr("运行时长") ,
                                  value:formatTime(CasterResourceController.cluster_runsec),
                                  percent:0 })




        }
    }

    ListModel
    {
        id:cluster_model

        ListElement{key:qsTr("负载") ; value:qsTr("-"); percent:0 }
        ListElement{key:qsTr("在线基站") ; value:qsTr("-"); percent:0 }
        ListElement{key:qsTr("在线移动站") ; value:qsTr("-"); percent:0 }
        ListElement{key:qsTr("节点状态") ; value:qsTr("-"); percent:0 }
        ListElement{key:qsTr("内存占用") ; value:qsTr("-"); percent:0 }
        ListElement{key:qsTr("下行") ; value:qsTr("-"); percent:0 }
        ListElement{key:qsTr("上行") ; value:qsTr("-"); percent:0 }
        ListElement{key:qsTr("运行时长") ; value:qsTr("-"); percent:0 }
    }


    GridView
    {
        Layout.fillWidth: true
        Layout.preferredHeight: contentHeight
        Layout.leftMargin: 10
        Layout.rightMargin: 10

        cellHeight: 160
        cellWidth: 380

        model:cluster_model

        delegate:Frame
        {
            width: 370
            height: 150

            Item
            {
                visible: model.percent!==0

                width: parent.height-30
                height: parent.height-30
                anchors{
                    verticalCenter: parent.verticalCenter
                    right: parent.right
                    rightMargin: 20
                }
                ProgressRing{
                    width: parent.width
                    height: parent.height
                    strokeWidth:15
                    from: 0
                    to: 1
                    value:model.percent/100.0
                    anchors.verticalCenter: parent.verticalCenter
                }

                RowLayout{
                    anchors.centerIn: parent
                    Label{
                        text: model.percent.toFixed(0)
                        font.pixelSize: 30         // 设置字体大小（像素）
                        font.bold: true            // 加粗

                        Layout.alignment: Qt.AlignBottom
                    }
                    Label{
                        text: qsTr("%")
                        font.pixelSize: 15
                        font.bold: true

                        Layout.alignment: Qt.AlignBottom
                        Layout.bottomMargin: 3
                    }
                }
            }

            Item
            {
                width: 200
                height: 80

                anchors{
                    bottom: parent.bottom
                    bottomMargin: 10
                    left: parent.left
                    leftMargin: 10
                }

                ColumnLayout{
                    anchors{
                        left: parent.left
                        leftMargin: 10
                        verticalCenter: parent.verticalCenter
                    }

                    Label{
                        text: model.key
                        font.pixelSize: 15         // 设置字体大小（像素）
                        font.bold: true            // 加粗
                        Layout.alignment: Qt.AlignBottom
                    }
                    Label{
                        text: model.value   ///负载适中/负载偏高/负载紧张/超过过高
                        font.pixelSize: 30         // 设置字体大小（像素）
                        font.bold: true            // 加粗
                        Layout.alignment: Qt.AlignBottom
                    }
                }
            }
        }
    }


    Label{
        text: qsTr("概览")
        font.pixelSize: 22         // 设置字体大小（像素）
        font.bold: true            // 加粗
        Layout.topMargin: 10
        Layout.bottomMargin: 10
        Layout.leftMargin: 20
    }

    GridView
    {
        Layout.fillWidth: true
        Layout.preferredHeight: contentHeight
        Layout.leftMargin: 10
        Layout.rightMargin: 10


        cellHeight: 130
        cellWidth:210

        model:    ListModel
        {
            //
            ListElement{key:qsTr("已注册账号数") ; value:qsTr("0");}
            ListElement{key:qsTr("即将过期") ; value:qsTr("0"); }
            ListElement{key:qsTr("已过期账号") ; value:qsTr("0");}
            ListElement{key:qsTr("数据接入") ; value:qsTr("0");  }
            ListElement{key:qsTr("数据推送") ; value:qsTr("0");  }
            ListElement{key:qsTr("策略组") ; value:qsTr("0");  }
            ListElement{key:qsTr("异常事件") ; value:qsTr("0");  }

        }


        delegate:Frame
        {
            width: 200
            height: 120

            ColumnLayout{
                anchors{
                    centerIn: parent
                }

                Label{
                    text: model.key
                    font.pixelSize: 15         // 设置字体大小（像素）
                    font.bold: true            // 加粗
                    Layout.alignment: Qt.AlignBottom
                }
                Label{
                    text: model.value   ///负载适中/负载偏高/负载紧张/超过过高
                    font.pixelSize: 30         // 设置字体大小（像素）
                    font.bold: true            // 加粗
                    Layout.alignment: Qt.AlignBottom
                }
            }
        }
    }




    Label{
        text: qsTr("节点状态")
        font.pixelSize: 22         // 设置字体大小（像素）
        font.bold: true            // 加粗
        Layout.topMargin: 10
        Layout.bottomMargin: 10
        Layout.leftMargin: 20
    }

    GridView{
        Layout.fillWidth: true
        Layout.preferredHeight: contentHeight
        Layout.leftMargin: 10
        Layout.rightMargin: 10
        cellHeight: 370
        cellWidth: 300
        model: dataModel

        // model:    ListModel
        // {
        //     //

        //     ListElement{key:qsTr("负载") ; value:qsTr("运行流畅"); percent:15.3 }
        //     ListElement{key:qsTr("在线基站") ; value:qsTr("20000"); percent:35.3 }
        //     ListElement{key:qsTr("在线移动站") ; value:qsTr("85134"); percent:16.3 }
        //     ListElement{key:qsTr("节点状态") ; value:qsTr("4/4"); percent:100 }
        //     ListElement{key:qsTr("内存占用") ; value:qsTr("556.32MB"); percent:0 }
        //     ListElement{key:qsTr("下行") ; value:qsTr("152.23Mbps"); percent:0 }
        //     ListElement{key:qsTr("上行") ; value:qsTr("282.15Mbps"); percent:0 }
        //     ListElement{key:qsTr("运行时长") ; value:qsTr("36d 15:21:14"); percent:0 }

        // }
        interactive: false
        delegate: com_item
    }

    Component{
        id:com_item
        Frame{
            width: 280
            height: 360

            Frame
            {
                width: parent.width
                height: 30

                anchors{
                    top: parent.top
                }


                IconButton
                {
                    anchors.centerIn: parent
                    text: qsTr("节点ID: ")+ model.UID
                    font.pixelSize: 15         // 设置字体大小（像素）
                    font.bold: true            // 加粗


                    onClicked:
                    {
                        console.log(Util.safeStringify(model))
                    }

                }

            }



            ColumnLayout
            {
                anchors{
                    top: parent.top
                    topMargin: 40
                    left: parent.left
                    leftMargin: 10
                }

                spacing: 10

                Label{
                    text: "节点版本: "+ model.tag_version
                    font.bold: true            // 加粗
                }
                Label{
                    text: "运行平台: "+ model.run_platform
                    font.bold: true            // 加粗
                }
                Label{
                    text: "连接数量: "+model.connnect_count+ " ( " +model.server_count + " 基站 " + model.client_count +" 移动站)";
                    font.bold: true            // 加粗
                }
                Label{
                    text: "节点负载: "+ model.cpu_usage.toFixed(2) + "%"
                    font.bold: true            // 加粗
                }
                Label{
                    text: "内存占用: "+formatBytes(model.mem_usage)
                    font.bold: true            // 加粗
                }
                Label{
                    text: "处理延迟: "+formatDelay(model.queue_delay)
                    font.bold: true            // 加粗
                }
                Label{
                    text: "网络延迟: PUB "+ formatDelay(model.pub_tcp_delay) + " / SUB "+ formatDelay(model.sub_tcp_delay)
                    font.bold: true            // 加粗
                }
                Label{
                    text: "数据延迟: PUB "+ formatDelay(model.pub_ping_delay) + " / SUB "+ formatDelay(model.sub_ping_delay)
                    font.bold: true            // 加粗
                }
                Label{
                    text: "输入流量: "+formatBytes(model.recv_total) + " ( "+ formatBytes(model.recv_speed) + "/s )"
                    font.bold: true            // 加粗
                }
                Label{
                    text: "输出流量: "+formatBytes(model.send_total) + " ( "+ formatBytes(model.send_speed) + "/s )"
                    font.bold: true            // 加粗
                }
                Label{
                    text: "运行时长: "+formatTime(model.online_time)
                    font.bold: true            // 加粗
                }
            }


            Frame
            {
                width: parent.width
                height: 40

                anchors{
                    bottom: parent.bottom
                }

                Row{

                    anchors.centerIn: parent


                    IconButton
                    {
                        icon.source: FluentIcons.graph_Settings
                    }
                    IconButton
                    {
                        icon.source: FluentIcons.graph_Play
                    }
                }

            }



        }
    }
    function formatDelay(us) {
        if (us === 0)
            return " - "

        var k = 1000
        var sizes = ["us", "ms", "s"]

        var i = Math.floor(Math.log(us) / Math.log(k))
        var value = us / Math.pow(k, i)

        // 秒的话只保留 3 位毫秒更好，但保持统一写法
        return value.toFixed(1) + " " + sizes[i]
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

    function formatMbps(bytesPerSecond) {
        if (bytesPerSecond === 0)
            return "0 Mbps";

        var bitsPerSecond = bytesPerSecond * 8; // 字节 -> 比特
        var k = 1000; // 网络通常用 1000 为单位
        var sizes = ["bps", "Kbps", "Mbps", "Gbps", "Tbps"];
        var i = Math.floor(Math.log(bitsPerSecond) / Math.log(k));
        var value = bitsPerSecond / Math.pow(k, i);

        return value.toFixed(2) + " " + sizes[i];
    }

    function formatTime(onlineUtcSeconds) {
        // 当前时间（UTC 秒）
        var nowUtc = Math.floor(Date.now() / 1000)

        // 已在线秒数
        var seconds = nowUtc - onlineUtcSeconds
        if (seconds < 0)
            seconds = 0

        var day = Math.floor(seconds / 86400)        // 1 天 = 86400s
        var h = Math.floor((seconds % 86400) / 3600)
        var m = Math.floor((seconds % 3600) / 60)
        var s = seconds % 60

        var timeStr = String(h).padStart(2, "0")
                + ":" + String(m).padStart(2, "0")
                + ":" + String(s).padStart(2, "0")

        if (day > 0)
            return day + "d " + timeStr
        else
            return timeStr
    }

    function formatUsage(percent) {
        if (percent < 0) percent = 0;
        if (percent > 100) percent = 100;

        if (percent <= 10) return "空闲";
        if (percent <= 30) return "轻量";
        if (percent <= 50) return "正常";
        if (percent <= 70) return "中等";
        if (percent <= 85) return "较高";
        if (percent <= 95) return "高负载";
        return "接近饱和";
    }

    // Item{

    //     height: 30
    //     implicitWidth: Window.width-100

    //     Layout.topMargin: 10
    //     Layout.bottomMargin: 10
    //     Layout.leftMargin: 20


    //     TabBar {

    //         clip: true
    //         Repeater {
    //             model:     ListModel{
    //                 id: tab_model
    //                 ListElement{
    //                     title: "负载状态"
    //                 }
    //                 ListElement{
    //                     title: "连接数"

    //                 }
    //                 ListElement{
    //                     title: "网络占用"
    //                 }
    //                 ListElement{
    //                     title: "数据交换"
    //                 }
    //                 ListElement{
    //                     title: "内存占用"
    //                 }
    //             }
    //             TabButton {
    //                 id: btn_tab
    //                 text: model.title
    //                 font.pixelSize: 22         // 设置字体大小（像素）
    //                 font.bold: true            // 加粗
    //             }
    //         }


    //         ComboBox
    //         {
    //             anchors.right: parent.right

    //             model: ["1","2","3"]
    //         }


    //     }


    //     RowLayout{
    //         anchors.right:parent.right
    //         // rightPadding: 30


    //         spacing: 10
    //         Label{
    //             text: qsTr("节点:")
    //             font.pixelSize: 15         // 设置字体大小（像素）
    //             font.bold: true            // 加粗
    //             Layout.alignment: Qt.AlignVCenter
    //         }

    //         ComboBox
    //         {

    //             // width: 100
    //             // Layout.alignment: Qt.AlignRight

    //             model: ["ALL","节点1","节点2"]
    //         }

    //         Label{
    //             text: qsTr("范围:")
    //             font.pixelSize: 15         // 设置字体大小（像素）
    //             font.bold: true            // 加粗
    //             Layout.alignment: Qt.AlignVCenter
    //         }

    //         ComboBox
    //         {
    //             // Layout.alignment: Qt.AlignRight

    //             model: ["5min","15min","30min","1h","3h","6h","12h","24h","48h","72h"]
    //         }


    //     }}




    // Item {
    //     id: root
    //     implicitWidth: Window.width
    //     height: 260

    //     // 数据缓存
    //     property var cpuData: []
    //     property var memData: []
    //     property var chartLabels: []

    //     function initChart() {
    //         cpuData = []
    //         memData = []
    //         chartLabels = []

    //         let now = new Date()
    //         for (let i = 59; i >= 0; i--) {
    //             let t = new Date(now - i * 1000)
    //             chartLabels.push(t.toTimeString().substring(3, 8)) // MM:SS

    //             cpuData.push(0)
    //             memData.push(0)
    //         }
    //     }

    //     Chart {
    //         id: chart
    //         anchors{

    //             fill: parent
    //             // topMargin: 30
    //         }
    //         type: "line"

    //         datas: {
    //             return {
    //                 labels: root.chartLabels,
    //                 datasets: [
    //                     {
    //                         label: "CPU (%)",
    //                         data: root.cpuData,
    //                         fill: false,
    //                         borderColor: "rgb(75, 192, 192)",  // 青色
    //                         tension: 0.2
    //                     },
    //                     {
    //                         label: "Memory (MB)",
    //                         data: root.memData,
    //                         fill: false,
    //                         borderColor: "rgb(255, 99, 132)",  // 红色
    //                         tension: 0.2
    //                     }
    //                 ]
    //             }
    //         }

    //         options: {
    //             return {
    //                 maintainAspectRatio: false,
    //                 scales: {
    //                     y: {
    //                         suggestedMin: 0,
    //                         suggestedMax: 100   // 可修改，比如内存最大 16000MB
    //                     }
    //                 }
    //             }
    //         }
    //     }

    //     Timer {
    //         id: timer
    //         interval: 1000
    //         repeat: true

    //         onTriggered: {
    //             // 模拟 CPU 数据
    //             let cpu = Math.random() * 80 + 10   // CPU 10~90%
    //             // 模拟内存，例如 2GB ~ 6GB
    //             let mem = Math.random() * 4000 + 2000

    //             // 移除旧数据
    //             root.cpuData.shift()
    //             root.memData.shift()
    //             root.chartLabels.shift()

    //             // 添加新数据
    //             root.cpuData.push(cpu)
    //             root.memData.push(mem)

    //             let t = new Date()
    //             root.chartLabels.push(t.toTimeString().substring(3, 8))

    //             // 更新图表
    //             chart.animateToNewData()
    //         }
    //     }

    //     Component.onCompleted: {
    //         initChart()
    //         timer.start()
    //     }
    // }

}

