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
        refreshDataOpUid = CasterMonitor.addRefreshAccountOperate()
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

    AccountDataController {
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
            focusItem=CasterMonitor.getUserAccountInfo(focusItemUID);
        }
    }

    onFocusItemUIDChanged: {
        console.log("onFocusItemUIDChanged: " + focusItemUID)

        focusItem=CasterMonitor.getUserAccountInfo(focusItemUID);

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
                        id:menu_bar


                        Menu {
                            width: 140
                            title: qsTr("账号注册")
                            MenuItem{
                                // icon.name:  FluentIcons.graph_Info
                                text:qsTr("账号注册")
                                onTriggered:{

                                    Global.open_dialog("/monitor/dialog/account/add_account","")
                                }
                            }

                            MenuSeparator { }

                            MenuItem{
                                // icon.name:  FluentIcons.graph_Info
                                text:qsTr("批量注册")
                                onTriggered:{
                                }
                            }

                            MenuSeparator { }


                        }
                        Menu {
                            width: 140
                            title: qsTr("用户管理")
                            MenuItem{
                                // icon.name:  FluentIcons.graph_Info
                                text:qsTr("激活/停用账号")
                                onTriggered:{
                                }
                            }
                            MenuSeparator { }

                            MenuItem{
                                // icon.name:  FluentIcons.graph_Info
                                text:qsTr("账号续期")
                                onTriggered:{
                                }
                            }
                        }
                        Menu {
                            width: 140
                            title: qsTr("显示")
                            MenuItem{
                                // icon.name:  FluentIcons.graph_Info
                                text:qsTr("显示过期账号")
                                onTriggered:{
                                }
                            }
                            MenuItem{
                                // icon.name:  FluentIcons.graph_Info
                                text:qsTr("显示正常账号")
                                onTriggered:{
                                }
                            }
                            MenuItem{
                                // icon.name:  FluentIcons.graph_Info
                                text:qsTr("显示匿名账号")
                                onTriggered:{
                                }
                            }
                        }
                        Menu {
                            width: 140
                            title: qsTr("筛选")
                            Menu{
                                width: 140
                                title: qsTr("条件筛选")
                                Action { text: qsTr("按照账号ID") }
                                Action { text: qsTr("按照机构") }
                                Action { text: qsTr("按照账号状态") }
                            }
                            MenuSeparator { }
                            Menu{
                                width: 140
                                title: qsTr("条件筛选")
                                Action { text: qsTr("按照账号ID") }
                                Action { text: qsTr("按照机构") }
                                Action { text: qsTr("按照账号状态") }
                            }
                        }
                    }                }

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
                            title: qsTr("账号")
                            dataIndex: "account"
                            width: 120
                            rowDelegate: function () {
                                return comp_mid_label
                            }
                            frozen: false
                        }
                        ListElement {
                            title: qsTr("账号类型")
                            dataIndex: "type"
                            width: 100
                            rowDelegate: function () {
                                return comp_type_label
                            }
                            frozen: false
                        }

                        ListElement {
                            title: qsTr("账号状态")
                            dataIndex: "UID"
                            width: 120
                            rowDelegate: function () {
                                return comp_state_label
                            }
                            frozen: false
                        }

                        ListElement {
                            title: qsTr("支持连接数")
                            dataIndex: "access_limit"
                            rowDelegate: function () {
                                return comp_mid_label
                            }
                            width: 100
                            frozen: false
                        }

                        ListElement {
                            title: qsTr("账号接入类型")
                            dataIndex: "access"
                            rowDelegate: function () {
                                return comp_access_label
                            }
                            width: 200
                            frozen: false
                        }

                        ListElement {
                            title: qsTr("注册日期")
                            dataIndex: "time_register"
                            rowDelegate: function () {
                                return comp_date_label
                            }
                            width: 180
                            frozen: false
                        }
                        ListElement {
                            title: qsTr("激活日期")
                            dataIndex: "time_active"
                            rowDelegate: function () {
                                return comp_date_label
                            }
                            width: 180
                            frozen: false
                        }
                        ListElement {
                            title: qsTr("失效日期")
                            dataIndex: "time_expired"
                            rowDelegate: function () {
                                return comp_date_label
                            }
                            width: 180
                            frozen: false
                        }


                        ListElement {
                            title: qsTr("用户名/机构名")
                            dataIndex: "contact_name"
                            rowDelegate: function () {
                                return comp_mid_label
                            }
                            width: 200
                        }
                        ListElement {
                            title: qsTr("联系人")
                            dataIndex: "contact_person"
                            rowDelegate: function () {
                                return comp_mid_label
                            }
                            width: 200
                        }
                        ListElement {
                            title: qsTr("联系方式")
                            dataIndex: "contact_info"
                            rowDelegate: function () {
                                return comp_mid_label
                            }
                            width: 200
                        }
                        ListElement {
                            title: qsTr("记录修改日期")
                            dataIndex: "time_modified"
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
                        text:qsTr("账号信息")

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

                            expanded:focusItemUID!==""
                            header: Label{
                                text: qsTr("基本信息")
                                font.weight: Font.Bold
                                verticalAlignment: Qt.AlignVCenter
                            }
                            content: com_account
                        }
                        ExpanderEx{
                            width: parent.width
                            expanderHeight:35

                            expanded:focusItemUID!==""
                            header: Label{
                                text: qsTr("账号状态")
                                font.weight: Font.Bold
                                verticalAlignment: Qt.AlignVCenter
                            }
                            content: com_state
                        }
                        ExpanderEx{
                            width: parent.width
                            expanderHeight:35

                            expanded:focusItemUID!==""
                            header: Label{
                                text: qsTr("账号归属")
                                font.weight: Font.Bold
                                verticalAlignment: Qt.AlignVCenter
                            }
                            content: com_from
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
        id:com_account
        Item{
            height: column.implicitHeight
            Column{
                id:column
                spacing: 3
                anchors.fill: parent
                ComItem{
                    item_name:qsTr("账号ID")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":focusItem.account
                    }
                }
                ComItem{
                    item_name:qsTr("接入类型")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":formatAccess(focusItem.access)
                    }
                }
                ComItem{
                    item_name:qsTr("注册日期")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":getLocalTime(focusItem.time_register)
                    }
                }
                ComItem{
                    item_name:qsTr("激活日期")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":getLocalTime(focusItem.time_active)
                    }
                }
                ComItem{
                    item_name:qsTr("失效日期")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":getLocalTime(focusItem.time_expired)
                    }
                }
            }
        }
    }

    Component{
        id:com_state
        Item{
            height: column.implicitHeight
            Column{
                id:column
                spacing: 3
                anchors.fill: parent
                ComItem{
                    item_name:qsTr("账号状态")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : formatState(focusItem.UID)
                    }
                }
                ComItem{
                    item_name:qsTr("在线连接数")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : formatState(focusItem.UID)
                    }
                }
                ComItem{
                    item_name:qsTr("剩余有效期")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : formatResTime(focusItem.time_expired)
                    }
                }
                ComItem{
                    item_name:qsTr("累计在线时长")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : formatState(focusItem.UID)
                    }
                }



            }
        }
    }


    Component{
        id:com_from
        Item{
            height: column.implicitHeight
            Column{
                id:column
                topPadding: 5
                spacing: 3
                anchors.fill: parent
                ComItem{
                    item_name:qsTr("数据供应商")
                    delegate:TextField{
                        text: focusItemUID===""?"":focusItem.contact_name
                    }
                }
                ComItem{
                    item_name:qsTr("联系人")
                    delegate:TextField{
                        text: focusItemUID===""?"":focusItem.contact_person
                    }
                }

                ComItem{
                    item_name:qsTr("联系方式")
                    delegate:TextField{
                        text: focusItemUID===""?"":focusItem.contact_info
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
        id:comp_fix4
        Item{
            Label{
                text: String(display.toFixed(4))
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
        id: comp_type_label

        DataItem {
            itemtext: formatType(display) // 传入 UTC 秒数
        }
    }


    Component{
        id: comp_state_label

        DataItem {
            itemtext: formatState(display) // 传入 UTC 秒数
        }
    }
    Component{
        id: comp_access_label

        DataItem {
            itemtext: formatAccess(display) // 传入 UTC 秒数
        }
    }

    function formatType(type)
    {
        switch (type) {
        case 0:
            return qsTr("永久账号")
        case 1:
            return qsTr("期限账号(失效日期)")
        case 2:
            return qsTr("期限账号(激活天数)")
        case 3:
            return qsTr("时限账号(在线时长)")
        default:
            return qsTr("未知")
        }
    }


    function formatState(UID)
    {

       var info=CasterMonitor.getUserAccountInfo(UID)

        var data=new Date()
        var utc = Math.floor(data.getTime() / 1000)

        if(info.state===0)
        {
            //已经停用

            if(info.type===0)
            {
                return qsTr("未启用")
            }

            if(info.time_expired>data)
            {
                return qsTr("未启用(已过期)")
            }
            else if(info.time_active===0)
            {
                return qsTr("未启用(未激活)")
            }
            else
            {
                return qsTr("未启用(已激活)")
            }
        }
        else if(info.state===1)
        {
            // 已经启用
            if(info.type===0)
            {
                return qsTr("已启用")
            }

            if(info.time_expired < data)
            {
                return qsTr("已启用(已过期)")
            }
            else if(info.time_active===0)
            {
                return qsTr("已启用(未激活)")
            }
            else
            {
                return qsTr("已启用(已激活)")
            }
        }
        else
        {
            return qsTr("未知")
        }
    }



    function formatAccess(type)
    {

        switch (type) {
        case 1:
            return qsTr("Ntrip Server/Client")
        case 2:
            return qsTr("Ntrip1.0/2.0 Client")
        case 3:
            return qsTr("Ntrip1.0 Server")
        case 4:
            return qsTr("Ntrip2.0 Server")
        default:
            return qsTr("未知")
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
                    localDate.getSeconds()).padStart(2, "0") //+ "." + ms
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

    function formatResTime(utcSeconds) {
        // 当前时间（UTC 秒）
        var nowUtc = Math.floor(Date.now() / 1000)

        // 已在线秒数
        var seconds = utcSeconds-nowUtc
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
