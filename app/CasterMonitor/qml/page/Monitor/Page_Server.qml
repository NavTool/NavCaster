import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import FluentUI.Controls
import FluentUI.impl
import CasterMonitor
import "../../extra"

Frame {
    id: root

    property string title
    property PageContext context

    // 布局属性
    anchors.fill: parent
    property int item_name_width:body_extra.width*0.35
    property int item_value_width:body_extra.width*0.65
    property int item_height:35


    //数据属性
    // 页面保存的上下文
    property string focusItemUID: ""   // 当前选定的数据记录的UID
    property var focusItem          // 选定记录的详细数据
    property var refreshDataOpUid      // 数据刷新操作的UID 重复调用这个UID指向的任务来刷新数据

    Component.onCompleted: {

        // focusItem= CasterMonitor.getNtripServerInfo("")  //初始化，填充空白数据

        //创建刷新数据操作
        refreshDataOpUid = CasterMonitor.addRefreshServerOperate()
        console.log("refreshDataOpUid: ", refreshDataOpUid)

        // 执行这个指令
        CasterMonitor.excuteOperate(refreshDataOpUid)

        // 启动定时器，定期刷新数据
        data_refresh_timer.start()
    }

    Connections {
        target: CasterMonitor

        function onOperateFinished(taskID, success, info) {
            if (taskID !== refreshDataOpUid) {
                return  //非当前指令,跳过
            }
            // 执行数据刷新操作
            controllerData.loadData()
        }
    }

    ServerDataController {
        id: controllerData
        onLoadDataStart: {
            // panel_loading.visible = true
        }
        onLoadDataSuccess: {

            //保存上下文
            var oldY = dataGrid.view.contentY

            // 更新数据源
            // dataModel.sourceData = data

            const oldCount = dataModel.count;
            const newCount = data.length;

            // 1. 先删除多余的项
            if (oldCount > newCount) {
                for (var i = oldCount - 1; i >= newCount; --i)
                    dataModel.remove(i);
            }

            // 2. 更新已有数据
            for (var row = 0; row < Math.min(oldCount, newCount); ++row)
                dataModel.set(row, data[row]);

            // 3. 添加新项
            for (var i = oldCount; i < newCount; ++i)
                dataModel.append(data[i]);

            // 恢复上下文
            dataGrid.view.contentY = oldY
            for (var i = 0; i < dataModel.count; ++i) {
                if (dataModel.get(i).UID === focusItemUID) {
                    dataGrid.view.currentIndex = i
                    // dataGrid.selected_items.clear()
                    dataGrid.selectionModel.select(dataModel.index(i, 0),
                                                   ItemSelectionModel.Select)
                }
                else{
                    dataGrid.selectionModel.select(dataModel.index(i, 0),
                                                   ItemSelectionModel.Deselect)
                }
            }

            //刷新选定条目的数据
            focusItem=CasterMonitor.getNtripServerInfo(focusItemUID);
        }
    }

    onFocusItemUIDChanged: {
        console.log("onFocusItemUIDChanged: " + focusItemUID)

        focusItem=CasterMonitor.getNtripServerInfo(focusItemUID);

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
    SplitView {
        id: split_layout
        anchors{
            fill: parent
            // topMargin: 65
        }
        orientation: Qt.Horizontal

        Frame {
            clip: true
            // visible:Global.visable_mid_side
            SplitView.fillWidth: true
            SplitView.fillHeight: true

            Frame
            {
                width: parent.width
                height: 60
                RowLayout{

                    anchors.centerIn:  parent
                    width: parent.width-30

                    // leftPadding: 20
                    spacing: 5

                    ComboBox{
                        implicitWidth: 150
                        implicitHeight: 35

                        model: ["按挂载点名筛选"]
                    }

                    // ComboBox{
                    //     implicitWidth: 150
                    //     implicitHeight: 35

                    //     model: ["筛选任务类型"]
                    // }

                    AutoSuggestBox {
                        id: auto_suggset_search
                        // width: 300
                        Layout.fillWidth: true
                        implicitHeight: 35
                        placeholderText: qsTr("Search")
                        items: controllerData.data
                        textRole: "login_mpt"
                        trailing: RowLayout {
                            IconButton {
                                implicitWidth: 30
                                implicitHeight: 20
                                icon.name: FluentIcons.graph_ChromeClose
                                icon.width: 10
                                icon.height: 10
                                visible: auto_suggset_search.text !== ""
                                onClicked: {
                                    auto_suggset_search.clear()
                                }
                            }
                            IconButton {
                                implicitWidth: 30
                                implicitHeight: 20
                                icon.name: FluentIcons.graph_Search
                                enabled: false
                                icon.width: 14
                                icon.height: 14
                            }
                        }
                        onTap: item => {
                                focusItemUID=item.UID

                                   for (var i = 0; i < dataModel.count; ++i) {
                                       if (dataModel.get(i).UID === focusItemUID) {
                                           dataGrid.view.currentIndex = i
                                           // dataGrid.selected_items.clear()
                                           dataGrid.selectionModel.select(dataModel.index(i, 0),
                                                                          ItemSelectionModel.Select)

                                                        dataGrid.view.contentY=i*40
                                       }
                                       else{
                                           dataGrid.selectionModel.select(dataModel.index(i, 0),
                                                                          ItemSelectionModel.Deselect)
                                       }
                                   }



                               }
                    }

                }


            }

            Frame {
                clip: true
                anchors{
                    fill: parent
                    // margins: 10
                    topMargin: 65
                }
                DataGrid {
                    id: dataGrid
                    anchors{
                        fill: parent
                        margins: 10
                        // topMargin: 70
                    }
                    sourceModel: dataModel

                    columnSourceModel: ListModel {
                        ListElement { frozen: false; width: 120 ; dataIndex: "login_mpt"    ; title: qsTr("接入挂载点")}
                        ListElement { frozen: false; width: 120 ; dataIndex: "alias_mpt"    ; title: qsTr("实际挂载点")}
                        ListElement { frozen: false; width: 120 ; dataIndex: "online_time"  ; title: qsTr("在线时长")}
                        ListElement { frozen: false; width: 150 ; dataIndex: "recv_speed"   ; title: qsTr("接收速度");}
                        ListElement { frozen: false; width: 150 ; dataIndex: "recv_total"   ; title: qsTr("累计接收");}
                        ListElement { frozen: false; width: 180 ; dataIndex: "llh_lat"      ; title: qsTr("纬度")}
                        ListElement { frozen: false; width: 180 ; dataIndex: "llh_lon"      ; title: qsTr("经度")}
                        ListElement { frozen: false; width: 100 ; dataIndex: "llh_height"   ; title: qsTr("高程")}

                        ListElement { frozen: false; width: 200 ; dataIndex: "update_time"  ; title: qsTr("数据更新时间")}
                    }
                    delegateProvider:
                        (dataIndex)=>{
                            switch(dataIndex){
                                case "online_time":
                                return comp_time_label

                                case "llh_lat":
                                return comp_lat2dms
                                case "llh_lon":
                                return comp_lon2dms
                                case "llh_height":
                                return comp_height
                                case "recv_speed":
                                return comp_str_speed
                                case "recv_total":
                                return comp_str_count
                                case "update_time":
                                return comp_date_label
                                default:
                                return comp_mid_label
                            }
                        }
                    columnHeaderProvider:
                        (dataIndex)=>{
                            switch(dataIndex){
                                case "avatar":
                                default:

                                return comp_mid_header
                                // return defaultColumnHeader
                            }
                        }
                    editDelegateProvider:
                        (dataIndex)=>{
                            switch(dataIndex){
                                case "action":
                                return undefined

                                default:
                                return undefined
                                // return defaultEditDelegate
                            }
                        }

                    onRowClicked: model => {
                                      // console.debug(model.station_name)

                                      Global.visable_right_side=true
                                      root.focusItemUID = model.UID
                                      console.log(Util.safeStringify(model))
                                  }
                    onRowRightClicked: model => {
                                           // console.debug(model.station_name)
                                           operate_item_menu.open_with_ctx(
                                               model)
                                       }

                    Menu {
                        property var ctx
                        id: operate_item_menu
                        width: 150
                        title: qsTr("操作站点")

                        function open_with_ctx(data) {
                            ctx = data
                            popup()
                        }

                        MenuItem {
                            text: qsTr("站点详情")
                            onTriggered: {
                                console.log(Util.safeStringify(
                                                operate_item_menu.ctx))
                            }
                        }
                    }



                }
            }
        }

        Frame {
            id: body_extra
            clip: true
            visible: Global.visable_right_side
            implicitWidth: body.width * 0.2 > 300 ? 300 : body.width * 0.2
            implicitHeight: body.height

            GroupBox{
                id:function_box
                anchors.fill: parent
                padding: 5
                Item{
                    width: parent.width
                    height: 30
                    Label{
                        anchors{
                            left: parent.left
                            leftMargin: 10
                            verticalCenter: parent.verticalCenter
                        }
                        text:qsTr("挂载点信息")

                        font:Qt.font({pixelSize : 15, weight: Font.Bold})
                    }

                    IconButton{
                        anchors{
                            right: parent.right
                            // rightMargin: 10
                            verticalCenter: parent.verticalCenter
                        }
                        icon.source: FluentIcons.graph_MiniExpand2Mirrored
                        icon.width: 15
                        icon.height: 15

                        onClicked: {
                            Global.visable_right_side=!Global.visable_right_side
                        }
                    }
                }


                Flickable{
                    anchors.fill: parent
                    anchors.topMargin: 35
                    contentHeight: columnItem.height
                    // contentHeight: rowItem.height
                    interactive: contentHeight > height
                    clip: true
                    Column{
                        id:columnItem
                        width: body_extra.width

                        ExpanderEx{
                            width: parent.width
                            expanderHeight:40

                            expanded:focusItemUID!==""
                            header: Label{
                                text: qsTr("基本信息")
                                font.weight: Font.Bold
                                verticalAlignment: Qt.AlignVCenter
                            }
                            content: com_station
                        }
                        ExpanderEx{
                            width: parent.width
                            expanderHeight:40

                            expanded:false
                            header: Label{
                                text: qsTr("数据详情")
                                font.weight: Font.Bold
                                verticalAlignment: Qt.AlignVCenter
                            }
                            content: com_data
                        }
                        ExpanderEx{
                            width: parent.width
                            expanderHeight:40

                            expanded:false
                            header: Label{
                                text: qsTr("设备信息")
                                font.weight: Font.Bold
                                verticalAlignment: Qt.AlignVCenter
                            }
                            content: com_device
                        }
                        ExpanderEx{
                            width: parent.width
                            expanderHeight:40

                            expanded:focusItemUID!==""
                            header: Label{
                                text: qsTr("连接信息")
                                font.weight: Font.Bold
                                verticalAlignment: Qt.AlignVCenter
                            }
                            content: com_connect
                        }
                        ExpanderEx{
                            width: parent.width
                            expanderHeight:40

                            expanded:false
                            header: Label{
                                text: qsTr("数据来源")
                                font.weight: Font.Bold
                                verticalAlignment: Qt.AlignVCenter
                            }
                            content: com_account
                        }
                        // ExpanderEx{
                        //     width: parent.width
                        //     expanderHeight:30

                        //     expanded:true
                        //     header: Label{
                        //         text: "Rinex输出的天线配置"
                        //         verticalAlignment: Qt.AlignVCenter
                        //     }
                        //     content: com_rinex
                        // }
                    }
                }
            }
        }
    }


    Component{
        id:com_station
        Item{
            height: column.implicitHeight
            Column{
                id:column
                spacing: 3
                anchors.fill: parent
                ComItem{
                    item_name:qsTr("挂载点名称")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":focusItem.login_mpt
                    }
                }
                ComItem{
                    item_name:qsTr("接入类型")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":format_model.get(focusItem.type).text

                        ListModel {
                            id: format_model
                            ListElement {text: qsTr("未知类型"); }
                            ListElement {text: qsTr("基站接入 (Ntrip Server)") ; }
                            ListElement {text: qsTr("系统生成 (最近挂载点模式)"); }
                            ListElement {text: qsTr("数据转发 (Ntrip Client)"); }
                            ListElement {text: qsTr("数据转发 (TCP Client)"); }
                            ListElement {text: qsTr("数据转发 (TCP Server)"); }
                            ListElement {text: qsTr("代理模式 (Ntrip Client)"); }
                            ListElement {text: qsTr("系统生成 (挂载点中转模式"); }
                        }


                    }
                }
                ComItem{
                    item_name:qsTr("站点纬度")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":lattoDMS(focusItem.llh_lat)
                    }
                }
                ComItem{
                    item_name:qsTr("站点经度")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":lontoDMS(focusItem.llh_lon)
                    }
                }
                ComItem{
                    item_name:qsTr("站点高程")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":(focusItem.llh_height.toFixed(4) + " m")
                    }
                }
            }
        }
    }

    Component{
        id:com_data
        Item{
            height: column.implicitHeight
            Column{
                id:column
                spacing: 3
                anchors.fill: parent
                ComItem{
                    item_name:qsTr("数据格式")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : qsTr("RTCM 3.3")
                    }
                }
                ComItem{
                    item_name:qsTr("数据流")
                    // height:multlinebox.Height
                    delegate:TextField{
                        id:multlinebox
                        placeholderText: "1006(10),1013(10),1019(68),1020(56),1033(12),1042(56),1044(56),1045(56)1046(56),1077(1),1087(1),1097(2),1117(1),1127(1),1230(10)"
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                        ToolTip.text: placeholderText
                    }

                }
                ComItem{
                    item_name:qsTr("信号类型")
                    delegate:TextField{
                        placeholderText: "GPS(L1/L2)\n BDS(B1I/B2I/B3I/B1C/B2a/B2b)"
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                        ToolTip.text: placeholderText
                    }
                }
                ComItem{
                    item_name:qsTr("信号计数")
                    delegate:TextField{
                        placeholderText: focusItemUID===""?"":focusItem.online_time
                    }
                }

            }
        }
    }

    Component{
        id:com_device

        Item{
            height: column.implicitHeight
            Column{
                id:column
                spacing: 3
                anchors.fill: parent
                ComItem{
                    item_name:qsTr("接收机类型")
                    delegate:TextField{
                        placeholderText: focusItemUID===""?"":""
                    }
                }
                ComItem{
                    item_name:qsTr("接收机版本号")
                    delegate:TextField{
                        placeholderText: focusItemUID===""?"":""
                    }
                }
                ComItem{
                    item_name:qsTr("接收机/SN")
                    delegate:TextField{
                        placeholderText: focusItemUID===""?"":""
                    }
                }
            }
        }
    }



    Component{
        id:com_connect

        Item{
            height: column.implicitHeight
            Column{
                id:column
                spacing: 3
                anchors.fill: parent

                ComItem{
                    item_name:qsTr("累计接收")
                    delegate:TextField{
                        placeholderText: focusItemUID===""?"":formatBytes(focusItem.recv_total)
                    }
                }
                ComItem{
                    item_name:qsTr("接收速度")
                    delegate:TextField{
                        placeholderText: focusItemUID===""?"":(formatBytes(focusItem.recv_speed)+"/s")
                    }
                }
                ComItem{
                    item_name:qsTr("累计发送")
                    delegate:TextField{
                        placeholderText: focusItemUID===""?"":formatBytes(focusItem.send_total)
                    }
                }
                ComItem{
                    item_name:qsTr("发送速度")
                    delegate:TextField{
                        placeholderText: focusItemUID===""?"":(formatBytes(focusItem.send_speed)+"/s")
                    }
                }
                ComItem{
                    item_name:qsTr("在线时长")
                    delegate:TextField{
                        placeholderText: focusItemUID===""?"":formatTime(focusItem.online_time)
                    }
                }
                ComItem{
                    item_name:qsTr("网络延迟")
                    delegate:TextField{
                        placeholderText: focusItemUID===""?"":formatDelay(focusItem.tcp_delay)
                    }
                }
                ComItem{
                    item_name:qsTr("上线时刻")
                    delegate:TextField{
                        placeholderText: focusItemUID===""?"":getLocalTime(focusItem.online_time)
                    }
                }
                ComItem{
                    item_name:qsTr("接入IP")
                    delegate:TextField{
                        text: focusItemUID===""?"":focusItem.ip
                    }
                }
                ComItem{
                    item_name:qsTr("接入端口")
                    delegate:TextField{
                        text: focusItemUID===""?"":focusItem.port
                    }
                }
            }
        }
    }

    Component{
        id:com_account
        Item{
            height: column.implicitHeight
            Column{
                id:column
                topPadding: 5
                bottomPadding: 5
                spacing: 3
                anchors.fill: parent
                ComItem{
                    item_name:qsTr("数据供应商")
                    delegate:TextField{
                        text: focusItemUID===""?"":"ComNav Tech"
                    }
                }
                ComItem{
                    item_name:qsTr("登录账号")
                    delegate:TextField{
                        text: focusItemUID===""?"":focusItem.account
                    }
                }

                ComItem{
                    item_name:qsTr("剩余有效期")
                    delegate:TextField{
                        text: focusItemUID===""?"":qsTr("永久账号")
                    }
                }
                ComItem{
                    item_name:qsTr("账号失效日期")
                    delegate:TextField{
                        text: focusItemUID===""?"":qsTr("永不失效")
                    }
                }
            }
        }
    }



    component ComItem:Item{
        property string item_name;
        property Component delegate

        width: item_name_width
        height:item_height
        // anchors.verticalCenter: parent.verticalCenter
        Row{
            spacing: 0
            Item{
                width: item_name_width
                height:item_height
                IconButton{
                    anchors.fill: parent
                    text: item_name
                }
            }
            Frame{
                width: item_value_width
                height:item_height
                AutoLoader{
                    anchors{
                        fill:parent
                    }
                    sourceComponent: delegate
                }
            }
        }
    }

    component DataItem: Item {
        property string itemtext
        Label {
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            anchors {
                verticalCenter: parent.verticalCenter
                // horizontalCenter: parent.horizontalCenter
                left: parent.left
                leftMargin: 10
                right: parent.right
                rightMargin: 10
            }
            elide: Label.ElideRight
            text: itemtext
        }
    }

    Component {
        id: comp_date_label
        Item {
            Label {
                anchors.centerIn: parent
                text: getLocalTime(display) // 传入 UTC 秒数
            }
        }
    }

    Component {
        id: comp_time_label
        DataItem {
            itemtext: formatTime(display) // 传入 UTC 秒数

        }
    }

    Component {
        id: comp_str_count
        DataItem {
            itemtext: formatBytes(display)

        }
    }

    Component {
        id: comp_str_speed
        DataItem {
            itemtext: formatBytes(display) + "/s"
        }
    }


    Component{
        id:comp_lat2dms
        Item{
            Label{
                text: lattoDMS(display)
                elide: Label.ElideRight
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                anchors{
                    verticalCenter: parent.verticalCenter
                    left: parent.left
                    leftMargin: 10
                    right: parent.right
                    rightMargin: 10
                }

            }
        }
    }

    Component{
        id:comp_lon2dms
        Item{
            Label{
                text: lontoDMS(display)
                elide: Label.ElideRight
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                anchors{
                    verticalCenter: parent.verticalCenter
                    left: parent.left
                    leftMargin: 10
                    right: parent.right
                    rightMargin: 10
                }

            }
        }
    }

    Component{
        id:comp_height
        Item{
            Label{
                text: String(display.toFixed(4)) + " m"
                elide: Label.ElideRight
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                anchors{
                    verticalCenter: parent.verticalCenter
                    left: parent.left
                    leftMargin: 10
                    right: parent.right
                    rightMargin: 10
                }
            }
        }
    }


    Component{
        id: comp_mid_label
        DataItem{
            itemtext: display
        }
    }

    Component{
        id: comp_mid_header
        Label{
            anchors.fill: parent
            text: columnModel.title
            verticalAlignment: Qt.AlignVCenter
            horizontalAlignment: Qt.AlignHCenter
            leftPadding: 10
            rightPadding: 10
            elide: Label.ElideRight
            font.bold: true
        }
    }
    Component{
        id: comp_type_label

        DataItem{
            itemtext: format_model.get(display).text//"xxxx"//display==  String(display)

            ListModel {
                id: format_model
                ListElement {text: qsTr("未知类型"); }
                ListElement {text: qsTr("实体站点") ; }
                ListElement {text: qsTr("最近基站模式"); }
                ListElement {text: qsTr("Ntrip Client接入"); }
                ListElement {text: qsTr("转发挂载点(TCP Client)"); }
                ListElement {text: qsTr("转发挂载点(Ntrip)"); }
            }
        }

    }

    function getLocalTime(utcSeconds) {
        if (utcSeconds === 0) {
            return "-"
        }

        var localDate = new Date(utcSeconds)
        // 注意：如果 utcSeconds 是秒，应该乘以 1000
        if (utcSeconds < 1e12) {
            // 如果是秒，需要乘以 1000
            localDate = new Date(utcSeconds * 1000)
        }

        let ms = String(localDate.getMilliseconds()).padStart(3,
                                                              "0")

        return localDate.getFullYear(
                    ) + "-" + String(localDate.getMonth(
                                         ) + 1).padStart(2, "0")
                + "-" + String(localDate.getDate()).padStart(
                    2, "0") + " " + String(
                    localDate.getHours()).padStart(2, "0")
                + ":" + String(localDate.getMinutes()).padStart(
                    2, "0") + ":" + String(
                    localDate.getSeconds()).padStart(2, "0") + "." + ms
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


    function formatDelay(us) {
        if (us === 0)
            return "0 us"

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

    function lontoDMS(degrees) {
        let isPositive = degrees >= 0;
        let direction = isPositive ? "E" : "W";

        // 1. 先取绝对值，再取度
        let absVal = Math.abs(degrees);
        let d = Math.floor(absVal);

        // 2. 分
        let remainder = (absVal - d) * 60;
        let m = Math.floor(remainder);

        // 3. 秒
        let sTotal = (remainder - m) * 60;
        let sInteger = Math.floor(sTotal);
        let sDecimal = (sTotal - sInteger).toFixed(5).slice(1);

        let sIntegerStr = sInteger.toString().padStart(2, '0');
        let degreeStr = d.toString().padStart(3, ' ');

        return `${degreeStr}° ${m.toString().padStart(2, '0')}' ${sIntegerStr}${sDecimal}" ${direction}`;
    }
    function lattoDMS(degrees) {
        // 判断正负，决定南北纬
        let isPositive = degrees >= 0;
        let direction = isPositive ? "N" : "S";

        // 1. 先取绝对值，再取整
        let absVal = Math.abs(degrees);
        let d = Math.floor(absVal);

        // 2. 分
        let remainder = (absVal - d) * 60;
        let m = Math.floor(remainder);

        // 3. 秒
        let sTotal = (remainder - m) * 60;
        let sInteger = Math.floor(sTotal);
        let sDecimal = (sTotal - sInteger).toFixed(5).slice(1);

        // 格式化
        let sIntegerStr = sInteger.toString().padStart(2, '0');
        let degreeStr = d.toString().padStart(3, ' ');

        return `${degreeStr}° ${m.toString().padStart(2, '0')}' ${sIntegerStr}${sDecimal}" ${direction}`;
    }


}
