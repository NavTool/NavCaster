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


    // 添加任务中间变量
    property var addAccountOpUid      // 数据刷新操作的UID 重复调用这个UID指向的任务来刷新数据
    property var delAccountOpUid      // 数据刷新操作的UID 重复调用这个UID指向的任务来刷新数据
    property var setAccountOpUid      // 数据刷新操作的UID 重复调用这个UID指向的任务来刷新数据



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
            if (taskID === refreshDataOpUid) {
                controllerData.loadData()
            }
            if(taskID===addAccountOpUid)
            {
                if(success)
                {
                    tip_top.showSuccess(qsTr("账号已添加"))
                    visable_right_side=false
                }
            }
            if(taskID=== delAccountOpUid)
            {
                if(success)
                {
                    tip_top.showSuccess(qsTr("账号已移除"))
                }
            }
            if(taskID=== setAccountOpUid)
            {
                if(success)
                {
                    tip_top.showSuccess(qsTr("账号已修改"))
                }
            }

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

                        model: ["按用户账号筛选"]
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
                        textRole: "account"
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



                    Button
                    {
                        implicitHeight: 35
                        implicitWidth: 120
                        icon.name: FluentIcons.graph_Add
                        icon.width: 20
                        icon.height: 20

                        text: "添加账号"
                        font.pixelSize: 15
                        font.bold: true            // 加粗

                        // highlighted: true

                        onClicked: {
                            Global.open_dialog("/monitor/dialog/account/add_account","")

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
                        ListElement { frozen: false; width: 120 ; dataIndex: "account"       ; title: qsTr("账号")}
                        ListElement { frozen: false; width: 180 ; dataIndex: "contact_name"  ; title: qsTr("用户名/机构名")}
                        ListElement { frozen: false; width: 90 ; dataIndex: "access_limit"  ; title: qsTr("支持连接数")}
                        ListElement { frozen: false; width: 200 ; dataIndex: "access"        ; title: qsTr("准入类型");}
                        ListElement { frozen: false; width: 100 ; dataIndex: "type"          ; title: qsTr("账号类型")}
                        ListElement { frozen: false; width: 120 ; dataIndex: "state"         ; title: qsTr("启用状态")}
                        ListElement { frozen: false; width: 120 ; dataIndex: "time_active"   ; title: qsTr("激活状态");}
                        ListElement { frozen: false; width: 120 ; dataIndex: "time_expired"  ; title: qsTr("可用状态")}
                        ListElement { frozen: false; width: 200 ; dataIndex: "time_register" ; title: qsTr("注册日期")}
                        ListElement { frozen: false; width: 200 ; dataIndex: "time_modified" ; title: qsTr("记录修改日期")}

                    }


                    delegateProvider:
                        (dataIndex)=>{
                            switch(dataIndex){
                                case "access":
                                return comp_access_label
                                case "type":
                                return comp_type_label
                                case "state":
                                return comp_state_label
                                case "time_active":
                                return comp_active_label
                                case "time_expired":
                                return comp_expired_label
                                case "time_register":
                                case "time_modified":
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
                                           root.focusItemUID = model.UID
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
                            text: qsTr("删除账号")
                            onTriggered: {
                                confirm_dialog.open()
                            }

                            ContentDialog{
                                id: confirm_dialog

                                x: Math.ceil((parent.width - width) / 2)
                                y: Math.ceil((parent.height - height) / 2)
                                parent: Overlay.overlay
                                dim: true
                                modal: true
                                title: qsTr("确认删除账号:[ %1 ] ？").arg(root.focusItemUID)

                                standardButtons:Dialog.Cancel
                                footer:DialogButtonBox {
                                    Button {
                                        text: qsTr("删除")
                                        onClicked: {
                                            var item= CasterMonitor.genAccountTemp()
                                            item.UID = root.focusItemUID

                                            console.log(Util.safeStringify(item))

                                            root.delAccountOpUid= CasterMonitor.addDelAccountOperate(item)

                                            CasterMonitor.excuteOperate(delAccountOpUid)

                                            confirm_dialog.close()

                                        }
                                    }
                                }
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
                            content: com_account
                        }
                        ExpanderEx{
                            width: parent.width
                            expanderHeight:40

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
                            expanderHeight:40

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
        id: comp_active_label

        DataItem {
            itemtext: formatActive(display) // 传入 UTC 秒数
        }
    }
    Component{
        id: comp_expired_label

        DataItem {
            itemtext: formatExpired(display) // 传入 UTC 秒数
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
            return qsTr("长期")
        case 1:
            return qsTr("期限")
        case 2:
            return qsTr("期限")
        case 3:
            return qsTr("时限")
        default:
            return qsTr("未知")
        }
    }


    function formatState(state)
    {
        if(state===0)
        {
            return qsTr("已停用")
        }
        else
        {
            // 已经启用
            return qsTr("已启用")
        }
    }

    function formatActive(state)
    {
        if(state===0)
        {
            return qsTr("未激活")
        }
        else
        {
            // 已经启用
            return qsTr("已激活")
        }
    }
    function formatExpired(expireUtcSeconds)
    {

        if(expireUtcSeconds===0)
        {
            return "正常"
        }

        // 当前 UTC 秒
        var nowUtc = Math.floor(Date.now() / 1000)
        // 剩余秒数
        var remaining = expireUtcSeconds - nowUtc

        if (remaining <= 0) {
            return "已过期"
        } else if (remaining < 24 * 3600) {
            return "不足1天"
        } else if (remaining < 3 * 24 * 3600) {
            return "不足3天"
        } else if (remaining < 7 * 24 * 3600) {
            return "不足7天"
        } else {
            return "正常"
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
