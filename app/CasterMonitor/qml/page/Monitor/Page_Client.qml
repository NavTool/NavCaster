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
                        ListElement { frozen: false; width: 120 ; dataIndex: "account"      ; title: qsTr("账号")}
                        ListElement { frozen: false; width: 120 ; dataIndex: "login_mpt"    ; title: qsTr("接入挂载点")}
                        ListElement { frozen: false; width: 120 ; dataIndex: "alias_mpt"    ; title: qsTr("使用挂载点")}
                        ListElement { frozen: false; width: 120 ; dataIndex: "online_time"  ; title: qsTr("在线时长");}
                        ListElement { frozen: false; width: 90  ; dataIndex: "quality"      ; title: qsTr("定位状态")}
                        ListElement { frozen: false; width: 90  ; dataIndex: "diff"         ; title: qsTr("差分延迟")}
                        ListElement { frozen: false; width: 180 ; dataIndex: "llh_lat"      ; title: qsTr("纬度");}
                        ListElement { frozen: false; width: 180 ; dataIndex: "llh_lon"      ; title: qsTr("经度")}
                        ListElement { frozen: false; width: 100 ; dataIndex: "llh_height"   ; title: qsTr("高程")}
                        ListElement { frozen: false; width: 200 ; dataIndex: "update_time"  ; title: qsTr("数据更新时间")}
                    }

                    delegateProvider:
                        (dataIndex)=>{
                            switch(dataIndex){
                                case "quality":
                                return comp_quality_label
                                case "diff":
                                return comp_diff_label
                                case "online_time":
                                return comp_time_label
                                case "update_time":
                                return comp_date_label
                                case "llh_lat":
                                return comp_lat2dms
                                case "llh_lon":
                                return comp_lon2dms
                                case "llh_height":
                                return comp_height


                                default:
                                return comp_mid_label
                                // return defaultDelegate
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
                        text:qsTr("用户信息")

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
                            content: com_login
                        }
                        ExpanderEx{
                            width: parent.width
                            expanderHeight:40

                            expanded:focusItemUID!==""
                            header: Label{
                                text: qsTr("定位状态")
                                font.weight: Font.Bold
                                verticalAlignment: Qt.AlignVCenter
                            }
                            content: com_status
                        }
                        ExpanderEx{
                            width: parent.width
                            expanderHeight:40

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
                            expanderHeight:40

                            expanded:focusItemUID!==""
                            header: Label{
                                text: qsTr("账号信息")
                                font.weight: Font.Bold
                                verticalAlignment: Qt.AlignVCenter
                            }
                            content: com_account
                        }
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
                    item_name:qsTr("用户名")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":focusItem.account
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
                            ListElement {text: qsTr("用户接入 (Ntrip Client)") ; }
                            ListElement {text: qsTr("用户接入 (最近挂载点模式)"); }
                            ListElement {text: qsTr("数据转发 (Ntrip Server)"); }
                            ListElement {text: qsTr("数据转发 (TCP Client)"); }
                            ListElement {text: qsTr("数据转发 (TCP Server)"); }
                            ListElement {text: qsTr("代理模式 (Ntrip Client)"); }
                        }
                    }
                }
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
                        placeholderText : focusItemUID===""?"":focusItem.alias_mpt
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
                        placeholderText : focusItemUID===""?"":formatQuality(focusItem.quality)


                    }
                }
                ComItem{
                    item_name:qsTr("用户经度")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":lontoDMS(focusItem.llh_lon)
                    }
                }
                ComItem{
                    item_name:qsTr("用户纬度")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":lattoDMS(focusItem.llh_lat)
                    }
                }
                ComItem{
                    item_name:qsTr("用户高程")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":(focusItem.llh_height.toFixed(4) + " m")
                    }
                }
                ComItem{
                    item_name:qsTr("基线长度")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":formatDistance(focusItem.distance)
                    }
                }
                ComItem{
                    item_name:qsTr("使用卫星数")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":focusItem.sat_num
                    }
                }
                ComItem{
                    item_name:qsTr("差分延迟")
                    delegate:TextField{
                        // placeholderText:GNSS.focusObsFile.station_name
                        placeholderText : focusItemUID===""?"":focusItem.diff.toFixed(1) + " s"
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
                    item_name:qsTr("数据延迟")
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
                spacing: 3
                anchors.fill: parent

                ComItem{
                    item_name:qsTr("登录账号")
                    delegate:TextField{
                        text: focusItemUID===""?"":focusItem.account
                    }
                }
                ComItem{
                    item_name:qsTr("账号类型")
                    delegate:TextField{
                        text: focusItemUID===""?"":"期限账号/永久账号/机构账号"
                    }
                }
                ComItem{
                    item_name:qsTr("归属用户/机构")
                    delegate:TextField{
                        text: focusItemUID===""?"":"测试"
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
        id: comp_quality_label
        DataItem {
            itemtext: formatQuality(display) // 传入 UTC 秒数
        }
    }

    Component {
        id: comp_diff_label
        DataItem {
            itemtext: display.toFixed(1) + " s"
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

    function formatDistance(distance) {
        // distance 可能是 number 或 string（可能带逗号）
        // 我们的规则：
        //  <1000(m) : 保留 1 位小数，单位 "m"
        // >=1000     : 转为 km，保留 2 位小数，单位 "km"
        //
        // 实现思路：
        // 1. 规范化输入（去掉已有逗号，转 Number）
        // 2. 根据大小决定小数位并用 toFixed() 得到字符串（避免科学计数法）
        // 3. 对整数部分按位插入逗号（从右往左每三位插入一个）
        // 4. 恢复小数部分和符号，返回最终字符串

        // 1) 规范化输入
        var s = String(distance)
        s = s.replace(/,/g, "")             // 去掉已有逗号
        var d = Number(s)
        if (!isFinite(d)) return ""         // 非数直接返回空字符串

        var negative = d < 0
        if (negative) d = -d

        // 2) 决定单位与小数位
        var unit = " m"
        var fracDigits = 1
        if (d >= 1000) {
            d = d / 1000.0
            unit = " km"
            fracDigits = 3
        }

        // 使用 toFixed 生成固定小数位的字符串（避免科学计数）
        // 注意：toFixed 在 JS 中对大数也能生成非科学计数法字符串
        var fixedStr = d.toFixed(fracDigits)  // 例如 "12025.01" 或 "987.6"

        // 3) 分离整数与小数部分
        var parts = fixedStr.split(".")
        var intPart = parts[0]
        var fracPart = (parts.length > 1) ? parts[1] : ""

        // 4) 从右到左每三位插入逗号
        var resInt = ""
        var count = 0
        for (var i = intPart.length - 1; i >= 0; --i) {
            resInt = intPart.charAt(i) + resInt
            count++
            if (count % 3 === 0 && i > 0) {
                resInt = "," + resInt
            }
        }

        // 5) 组合结果，保留小数部分（如果 fracDigits>0）
        var result = (negative ? "-" : "") + resInt
        if (fracDigits > 0) {
            result += "." + fracPart
        }
        result += unit

        return result
    }

    function formatQuality(type)
    {
        switch (type) {
        case 0:
            return qsTr("未定位")
        case 1:
            return qsTr("单点定位")
        case 2:
            return qsTr("差分定位")
        case 3:
            return qsTr("PPS fix")
        case 4:
            return qsTr("固定解")
        case 5:
            return qsTr("浮点解")
        case 6:
            return qsTr("估计模式")
        case 7:
            return qsTr("手工输入")
        case 8:
            return qsTr("仿真模式")
        default:
            return qsTr("未知状态")
        }
    }

}
