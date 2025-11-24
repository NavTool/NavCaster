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
    property int item_height:30


    //数据属性
    // 页面保存的上下文
    property string focusItemUID: ""   // 当前选定的数据记录的UID
    property var focusItem          // 选定记录的详细数据
    property var refreshDataOpUid      // 数据刷新操作的UID 重复调用这个UID指向的任务来刷新数据

    Component.onCompleted: {

        // focusItem= CasterMonitor.getNtripServerInfo("")  //初始化，填充空白数据

        //创建刷新数据操作
        refreshDataOpUid = CasterMonitor.addRefreshClientOperate()
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

    ClientDataController {
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
                    dataGrid.selectionModel.select(dataModel.index(i, 0),
                                                   ItemSelectionModel.Select)
                    break
                }
            }

            //刷新选定条目的数据
            focusItem=CasterMonitor.getNtripClientInfo(focusItemUID);
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
        anchors.fill: parent
        orientation: Qt.Horizontal

        Frame {
            clip: true
            // visible:Global.visable_mid_side
            SplitView.fillWidth: true
            SplitView.fillHeight: true

            Frame {
                id: header_action
                anchors {
                    top: parent.top
                    left: parent.left
                    // leftMargin: 20
                    right: parent.right
                }
                height: 40

                Row {
                    anchors {
                        verticalCenter: parent.verticalCenter
                        left: parent.left
                        leftMargin: 5
                    }
                    spacing: 5
                    MenuBar {
                        id: menu_bar

                        Menu {
                            width: 140
                            title: qsTr("显示")
                            MenuItem {
                                // icon.name:  FluentIcons.graph_Info
                                text: qsTr("显示过期账户")
                                onTriggered: {

                                }
                            }
                            MenuItem {
                                // icon.name:  FluentIcons.graph_Info
                                text: qsTr("显示正常账户")
                                onTriggered: {

                                }
                            }
                        }
                        Menu {
                            width: 140
                            title: qsTr("筛选")
                            Menu {
                                width: 140
                                title: qsTr("条件筛选")
                                Action {
                                    text: qsTr("按照账号ID")
                                }
                                Action {
                                    text: qsTr("按照机构")
                                }
                                Action {
                                    text: qsTr("按照账号状态")
                                }
                            }
                            MenuSeparator {}
                            Menu {
                                width: 140
                                title: qsTr("条件筛选")
                                Action {
                                    text: qsTr("按照账号ID")
                                }
                                Action {
                                    text: qsTr("按照机构")
                                }
                                Action {
                                    text: qsTr("按照账号状态")
                                }
                            }
                        }
                    }
                }

                Row {

                    anchors {
                        verticalCenter: parent.verticalCenter
                        right: parent.right
                        rightMargin: 10
                    }
                    spacing: 5

                    AutoSuggestBox {
                        id: auto_suggset_search
                        width: 300
                        placeholderText: qsTr("Search")
                        items: []
                        textRole: "title"
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
                                   if (item.key) {
                                       page_router.go(item.key)
                                   }
                               }
                        Connections {
                            target: navigation_view
                            function onSourceItemsChanged(data) {
                                auto_suggset_search.items = data.filter(
                                            item => {
                                                return item instanceof PaneItem
                                            })
                            }
                        }
                    }

                    Button {
                        width: 70
                        text: qsTr("检索")
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }

            Frame {
                anchors {
                    top: header_action.bottom
                    bottom: footer_action.top
                    left: parent.left
                    right: parent.right
                    leftMargin: 5
                    rightMargin: 5
                    topMargin: 5
                    bottomMargin: 5
                }

                DataGridEx {
                    id: dataGrid
                    anchors.fill: parent

                    // Pane {
                    //     id: panel_loading
                    //     anchors.fill: dataGrid
                    //     ProgressRing {
                    //         anchors.centerIn: parent
                    //         indeterminate: true
                    //     }
                    //     background: Rectangle {
                    //         color: Theme.res.solidBackgroundFillColorBase
                    //     }
                    // }
                    defaultHeight: 30
                    defaultminimumHeight: 25
                    defaultmaximumHeight: 240
                    horizonalHeaderHeight: 30

                    sourceModel: dataModel
                    onRowClicked: model => {
                                      // console.debug(model.station_name)
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

                    columnSourceModel: ListModel {

                        ListElement {
                            title: qsTr("账号ID")
                            dataIndex: "account"
                            width: 150
                        }

                        ListElement {
                            title: qsTr("账号机构")
                            dataIndex: "account"
                            width: 150
                        }
                       ListElement {
                            title: qsTr("定位状态")
                            dataIndex: "ecef_x"
                            width: 80
                            frozen: false
                        }
                        ListElement {
                            title: qsTr("差分延迟")
                            dataIndex: "ecef_x"
                            width: 80
                            frozen: false
                        }

                        ListElement {
                            title: qsTr("接入挂载点")
                            dataIndex: "login_mpt"
                            width: 150
                            frozen: false
                        }
                        ListElement {
                            title: qsTr("使用挂载点")
                            dataIndex: "alias_mpt"
                            width: 150
                            frozen: false
                        }

                        ListElement {
                            title: qsTr("在线时长")
                            dataIndex: "online_time"
                            width: 100
                            rowDelegate: function () {
                                return comp_time_label
                            }
                            frozen: false
                        }
                        ListElement {
                            title: qsTr("累计发送")
                            dataIndex: "send_total"
                            rowDelegate: function () {
                                return comp_str_count
                            }
                            width: 150
                            frozen: false
                        }
                        ListElement {
                            title: qsTr("发送速度")
                            dataIndex: "send_speed"
                            rowDelegate: function () {
                                return comp_str_speed
                            }
                            width: 150
                            frozen: false
                        }
                        ListElement {
                            title: qsTr("纬度")
                            dataIndex: "ecef_x"
                            width: 150
                            frozen: false
                        }

                        ListElement {
                            title: qsTr("经度")
                            dataIndex: "ecef_y"
                            width: 150
                            frozen: false
                        }

                        ListElement {
                            title: qsTr("椭球高")
                            dataIndex: "ecef_z"
                            width: 100
                            frozen: false
                        }
                        ListElement {
                            title: qsTr("IP")
                            dataIndex: "ip"
                            width: 120
                            frozen: false
                        }
                        ListElement {
                            title: qsTr("端口")
                            dataIndex: "port"
                            width: 120
                        }

                        ListElement {
                            title: qsTr("数据更新时间")
                            dataIndex: "update_time"
                            rowDelegate: function () {
                                return comp_date_label
                            }
                            width: 200
                        }
                    }
                }
            }

            Frame {
                id: footer_action
                anchors {
                    bottom: parent.bottom
                    left: parent.left
                    right: parent.right
                }
                height: 50

                Pagination {
                    pageCurrent: 1
                    pageButtonCount: 5
                    itemCount: 5000

                    anchors.horizontalCenter: parent.horizontalCenter

                    footer: ComboBox {

                        height: 30
                        width: 100

                        model: [qsTr("25条/页"), qsTr("50条/页"), qsTr(
                                "100条/页"), qsTr("500条/页"), qsTr("1000条/页")]
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
                        text:qsTr("接入用户信息")

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
                            expanderHeight:35

                            expanded:true
                            header: Label{
                                text: qsTr("接入信息")
                                font.weight: Font.Bold
                                verticalAlignment: Qt.AlignVCenter
                            }
                            content: com_login
                        }
                        ExpanderEx{
                            width: parent.width
                            expanderHeight:35

                            expanded:true
                            header: Label{
                                text: qsTr("定位状态")
                                font.weight: Font.Bold
                                verticalAlignment: Qt.AlignVCenter
                            }
                            content: com_status
                        }
                        ExpanderEx{
                            width: parent.width
                            expanderHeight:35

                            expanded:false
                            header: Label{
                                text: qsTr("连接信息")
                                font.weight: Font.Bold
                                verticalAlignment: Qt.AlignVCenter
                            }
                            content: com_connect
                        }
                        ExpanderEx{
                            width: parent.width
                            expanderHeight:35

                            expanded:true
                            header: Label{
                                text: qsTr("账号信息")
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
        id:com_login
        Item{
            height: column.implicitHeight
            Column{
                id:column
                spacing: 3
                anchors.fill: parent
                ComItem{
                    item_name:qsTr("接入挂载点")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":focusItem.login_mpt
                    }
                }
                ComItem{
                    item_name:qsTr("使用挂载点")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":focusItem.online_time
                    }
                }
                ComItem{
                    item_name:qsTr("站点经度")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":focusItem.ecef_x
                    }
                }
                ComItem{
                    item_name:qsTr("站点纬度")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":focusItem.ecef_y
                    }
                }
                ComItem{
                    item_name:qsTr("站点高程")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":focusItem.ecef_y
                    }
                }
            }
        }
    }

    Component{
        id:com_status
        Item{
            height: column.implicitHeight
            Column{
                id:column
                spacing: 3
                anchors.fill: parent
                ComItem{
                    item_name:qsTr("定位状态")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":focusItem.ecef_z
                    }
                }
                ComItem{
                    item_name:qsTr("用户经度")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":focusItem.ecef_x
                    }
                }
                ComItem{
                    item_name:qsTr("用户纬度")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":focusItem.ecef_y
                    }
                }
                ComItem{
                    item_name:qsTr("用户高程")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":focusItem.ecef_y
                    }
                }
                ComItem{
                    item_name:qsTr("基线长度")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":focusItem.ecef_z
                    }
                }
                ComItem{
                    item_name:qsTr("使用卫星数")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":focusItem.ecef_z
                    }
                }
                ComItem{
                    item_name:qsTr("差分延迟")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":focusItem.ecef_z
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
                    item_name:qsTr("上线时刻")
                    delegate:TextField{
                        placeholderText: focusItemUID===""?"":focusItem.online_time
                    }
                }
                ComItem{
                    item_name:qsTr("在线时长")
                    delegate:TextField{
                        placeholderText: focusItemUID===""?"":focusItem.online_time
                    }
                }
                ComItem{
                    item_name:qsTr("累计接收")
                    delegate:TextField{
                        placeholderText: focusItemUID===""?"":focusItem.recv_total
                    }
                }
                ComItem{
                    item_name:qsTr("接收速度")
                    delegate:TextField{
                        placeholderText: focusItemUID===""?"":focusItem.recv_speed
                    }
                }
                ComItem{
                    item_name:qsTr("累计发送")
                    delegate:TextField{
                        placeholderText: focusItemUID===""?"":focusItem.send_total
                    }
                }
                ComItem{
                    item_name:qsTr("发送速度")
                    delegate:TextField{
                        placeholderText: focusItemUID===""?"":focusItem.send_speed
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
                spacing: 3
                anchors.fill: parent

                ComItem{
                    item_name:qsTr("登录账号")
                    delegate:TextField{
                        text: focusItemUID===""?"":focusItem.account
                    }
                }
                ComItem{
                    item_name:qsTr("账号机构")
                    delegate:TextField{
                        text: focusItemUID===""?"":"ComNav Tech"
                    }
                }
                ComItem{
                    item_name:qsTr("剩余有效期")
                    delegate:TextField{
                        text: focusItemUID===""?"":"ComNav Tech"
                    }
                }
                ComItem{
                    item_name:qsTr("账号失效日期")
                    delegate:TextField{
                        text: focusItemUID===""?"":"ComNav Tech"
                    }
                }
            }
        }
    }



    Component{
        id:com_rinex
        Item{
            height: 120
            Column{
                spacing: 3
                anchors.fill: parent
                ComItem{
                    item_name:qsTr("测量方式")
                    delegate:TextField{
                        placeholderText: GNSS.focusObsFile.rinex_measurement_method
                    }
                }
                ComItem{
                    item_name:qsTr("天线高")
                    delegate:TextField{
                        placeholderText: GNSS.focusObsFile.rinex_ant_height
                    }
                }
                ComItem{
                    item_name:qsTr("厂商")
                    delegate:TextField{
                        placeholderText: GNSS.focusObsFile.rinex_manufacturer
                    }
                }
                ComItem{
                    item_name:qsTr("天线类型")
                    delegate:TextField{
                        placeholderText: GNSS.focusObsFile.rinex_ant_type
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
            }
        }
    }

    Component {
        id: comp_time_label
        DataItem {
            itemtext: formatTime(display) // 传入 UTC 秒数
            function formatTime(onlineUtcSeconds) {
                // 当前时间的 UTC 秒数
                var nowUtc = Math.floor(Date.now() / 1000)

                // 在线秒数
                var seconds = nowUtc - onlineUtcSeconds
                if (seconds < 0)
                    seconds = 0 // 防止上线时间晚于当前时间

                // 格式化 hh:mm:ss
                var h = Math.floor(seconds / 3600)
                var m = Math.floor((seconds % 3600) / 60)
                var s = seconds % 60

                return String(h).padStart(2, "0") + ":" + String(m).padStart(
                            2, "0") + ":" + String(s).padStart(2, "0")
            }
        }
    }

    Component {
        id: comp_str_count
        DataItem {
            itemtext: formatBytes(display)
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
    }

    Component {
        id: comp_str_speed
        DataItem {
            itemtext: formatBytes(display) + "/s"
            function formatBytes(bytes) {
                if (bytes === 0)
                    return "0 B"
                var k = 1024
                var sizes = ["Byte", "KB", "MB", "GB", "TB"]
                var i = Math.floor(Math.log(bytes) / Math.log(k))
                var value = bytes / Math.pow(k, i)
                return value.toFixed(2) + " " + sizes[i]
            }
        }
    }
}
