import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import FluentUI.Controls
import FluentUI.impl
import CasterMonitor
import "../../extra"

Item {
    id: root

    property string title
    property PageContext context

    // 布局属性
    anchors.fill: parent
    property int item_name_width:body_extra.width*0.35
    property int item_value_width:body_extra.width*0.65
    property int item_height:30

    property bool visable_right_side:false

    //数据属性
    // 页面保存的上下文
    property string focusItemUID: ""   // 当前选定的数据记录的UID
    property var focusItem          // 选定记录的详细数据
    property var refreshDataOpUid      // 数据刷新操作的UID 重复调用这个UID指向的任务来刷新数据




    // 添加任务中间变量
    property var addTaskOpUid      // 数据刷新操作的UID 重复调用这个UID指向的任务来刷新数据


    property int    type: 0           //数据接入类型
    property string target_ip: ""
    property int    target_port: 0
    property string target_mpt: ""
    property string target_account: ""
    property string target_password: ""
    property string login_mpt: ""


    Component.onCompleted: {


        //创建刷新数据操作
        refreshDataOpUid = CasterMonitor.addRefreshRelayPullOperate()
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
            if(taskID===addTaskOpUid)
            {
                if(success)
                {

                    tip_top.showSuccess(qsTr("账号添加完成"))
                    visable_right_side=false

                }
            }

            // 执行数据刷新操作

        }
    }


    PullDataController {
        id: controllerData
        onLoadDataStart: {
            // panel_loading.visible = true
        }
        onLoadDataSuccess: {

            //保存上下文
            // var oldY = dataGrid.view.contentY

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
            // dataGrid.view.contentY = oldY
            // for (var i = 0; i < dataModel.count; ++i) {
            //     if (dataModel.get(i).UID === focusItemUID) {
            //         dataGrid.view.currentIndex = i
            //         dataGrid.selectionModel.select(dataModel.index(i, 0),
            //                                        ItemSelectionModel.Select)
            //         break
            //     }
            // }

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

                model: ["筛选任务状态"]
            }

            // ComboBox{
            //     implicitWidth: 150
            //     implicitHeight: 35

            //     model: ["筛选任务类型"]
            // }

            TextBox
            {
                Layout.fillWidth: true

                // anchors.verticalCenter: parent.verticalCenter

                implicitHeight: 35
                trailing: IconButton{
                    implicitWidth: 30
                    implicitHeight: 20
                    icon.name: FluentIcons.graph_Search
                    icon.width: 20
                    icon.height: 20
                    padding: 0
                }

                placeholderText: "查询已添加的数据任务"

            }

            Button
            {
                implicitHeight: 35
                implicitWidth: 120
                icon.name: FluentIcons.graph_Add
                icon.width: 20
                icon.height: 20

                text: "接入数据"
                font.pixelSize: 15
                font.bold: true            // 加粗

                // highlighted: true

                onClicked: {
                    root.visable_right_side=true;
                }
            }


        }

    }

    SplitView {
        id: split_layout
        anchors{
            fill: parent
            topMargin: 70
        }
        orientation: Qt.Horizontal

        Frame {
            clip: true
            // visible:Global.visable_mid_side
            SplitView.fillWidth: true
            SplitView.fillHeight: true



            Frame{
                width: parent.width
                height: 40


                RowLayout{

                    anchors{

                        fill: parent
                        leftMargin: 10
                        rightMargin: 10
                    }

                    Label{
                        text: "任务数: "
                        font.bold: true
                        font.pixelSize: 15
                    }
                }
            }

            Item{
                id: layout_column

                anchors{
                    fill: parent
                    margins: 5
                    topMargin: 45
                }

                GridView{

                    anchors.fill: parent
                    cellWidth: 230
                    cellHeight: 290

                    model: dataModel
                    interactive: false
                    delegate: Frame{
                        width: 220
                        height: 280

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
                                text: model.login_mpt
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
                                text: "数据类型: "+ model.type
                                font.bold: true            // 加粗
                            }
                            Label{
                                text: "挂载点名: "+ model.target_mpt
                                font.bold: true            // 加粗
                            }
                            Label{
                                text: "数据地址: "+ model.target_ip + ":"+model.target_port
                                font.bold: true            // 加粗
                            }
                            Label{
                                text: "使用账号: "+model.target_account
                                font.bold: true            // 加粗
                            }
                            Label{
                                text: "任务状态: 已启用/已禁用"
                                font.bold: true            // 加粗
                            }
                            Label{
                                text: "连接状态: 已连接/已断开"
                                font.bold: true            // 加粗
                            }
                            Label{
                                text: "流量统计: "
                                font.bold: true            // 加粗
                            }
                            Label{
                                text: "连接时长: "
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



            }


        }

        Frame {
            id: body_extra
            clip: true
            visible: root.visable_right_side
            implicitWidth: body.width * 0.3
            implicitHeight: body.height


            Component.onCompleted: {


                var item= CasterMonitor.genPullStreamTemp()

                console.log(Util.safeStringify(item))

            }


            Column{
                anchors{
                    fill: parent
                    margins: 20
                }

                spacing: 10

                Item{
                    implicitWidth: 120
                    height: 30
                    Label{
                        anchors.centerIn: parent
                        text: "数据接入信息"
                        font.pixelSize: 20
                        font.bold: true
                    }
                }

                RowLayout{
                    width: parent.width
                    spacing: 10

                    Item{
                        implicitWidth: 150
                        Label{
                            anchors.centerIn: parent
                            text: "接入数据类型"
                        }
                    }
                    ComboBox{
                        id:type_combobox

                        Layout.fillWidth: true
                        model:["NTRIP Client 1.0","NTRIP Client 2.0","TCP Client","TCP Server"]
                        onCurrentIndexChanged: {
                            root.type=currentIndex+1
                        }
                        Connections{
                            target: root
                            function onVisable_right_sideChanged()
                            {
                                if(root.visable_right_side==false)
                                {
                                    type_combobox.currentIndex=0
                                }
                            }
                        }
                    }
                }
                RowLayout{
                    width: parent.width
                    spacing: 10

                    Item{
                        implicitWidth: 150
                        Label{
                            anchors.centerIn: parent
                            text: "数据源IP"
                        }
                    }
                    TextBox{
                        id:ip_textbox
                        Layout.fillWidth: true

                        onTextChanged: {
                            root.target_ip=text
                        }
                        Connections{
                            target: root
                            function onVisable_right_sideChanged()
                            {
                                if(root.visable_right_side==false)
                                {
                                    ip_textbox.text=""
                                }
                            }
                        }
                    }
                }
                RowLayout{
                    width: parent.width
                    spacing: 10

                    Item{
                        implicitWidth: 150
                        Label{
                            anchors.centerIn: parent
                            text: "数据源端口"
                        }
                    }
                    TextBox{
                        id:port_textbox
                        Layout.fillWidth: true

                        onTextChanged: {
                            root.target_port=Number(text)
                        }
                        Connections{
                            target: root
                            function onVisable_right_sideChanged()
                            {
                                if(root.visable_right_side==false)
                                {
                                    port_textbox.text=""
                                }
                            }
                        }
                    }
                }

                RowLayout{
                    width: parent.width
                    spacing: 10

                    Item{
                        implicitWidth: 150
                        Label{
                            anchors.centerIn: parent
                            text: "挂载点名称"
                        }
                    }
                    TextBox{
                        id:mpt_textbox
                        Layout.fillWidth: true
                        onTextChanged: {
                            root.target_mpt=text
                        }
                        Connections{
                            target: root
                            function onVisable_right_sideChanged()
                            {
                                if(root.visable_right_side==false)
                                {
                                    mpt_textbox.text=""
                                }
                            }
                        }
                    }
                }

                RowLayout{
                    width: parent.width
                    spacing: 10

                    Item{
                        implicitWidth: 150
                        Label{
                            anchors.centerIn: parent
                            text: "登录用户名"
                        }
                    }
                    TextBox{
                        id: account_textbox
                        Layout.fillWidth: true
                        onTextChanged: {
                            root.target_account=text
                        }
                        Connections{
                            target: root
                            function onVisable_right_sideChanged()
                            {
                                if(root.visable_right_side==false)
                                {
                                    account_textbox.text=""
                                }
                            }
                        }
                    }
                }

                RowLayout{
                    width: parent.width
                    spacing: 10

                    Item{
                        implicitWidth: 150
                        Label{
                            anchors.centerIn: parent
                            text: "登录密码"
                        }
                    }
                    PasswordBox{
                        id:password_textbox
                        Layout.fillWidth: true
                        onTextChanged: {
                            root.target_password=text
                        }
                        Connections{
                            target: root
                            function onVisable_right_sideChanged()
                            {
                                if(root.visable_right_side==false)
                                {
                                    password_textbox.text=""
                                }
                            }
                        }
                    }
                }

                Item{
                    implicitWidth: 120
                    height: 30
                    Label{
                        anchors.centerIn: parent
                        text: "数据接入配置"
                        font.pixelSize: 20
                        font.bold: true
                    }
                }

                RowLayout{
                    width: parent.width
                    spacing: 10

                    Item{
                        implicitWidth: 150
                        Label{
                            anchors.centerIn: parent
                            text: "对外挂载点名"
                        }
                    }
                    TextBox{
                        id:localmpt_textbox
                        Layout.fillWidth: true
                        onTextChanged: {
                            root.login_mpt=text
                        }
                        Connections{
                            target: root
                            function onVisable_right_sideChanged()
                            {
                                if(root.visable_right_side==false)
                                {
                                    localmpt_textbox.text=""
                                }
                            }
                        }
                    }
                }


                RowLayout{
                    width: parent.width
                    spacing: 10

                    Item{
                        implicitWidth: 150
                        Label{
                            anchors.centerIn: parent
                            text: "首次断开重连间隔"
                        }
                    }
                    ComboBox{
                        Layout.fillWidth: true
                        model:["5s"]
                    }
                }


                RowLayout{
                    width: parent.width
                    spacing: 10

                    Item{
                        implicitWidth: 150
                        Label{
                            anchors.centerIn: parent
                            text: "最大重连间隔"
                        }
                    }
                    ComboBox{
                        Layout.fillWidth: true
                        model:["60s"]
                    }
                }

                RowLayout{
                    width: parent.width
                    spacing: 10

                    Item{
                        implicitWidth: 150
                        Label{
                            anchors.centerIn: parent
                            text: "最大重连次数"
                        }
                    }
                    ComboBox{
                        Layout.fillWidth: true
                        model:["无限制"]
                    }
                }
            }

            RowLayout{

                anchors{
                    bottom:parent.bottom
                    bottomMargin: 30
                }

                width: parent.width
                spacing: 10


                Button{
                    implicitHeight: 40
                    implicitWidth: 120

                    Layout.alignment: Qt.AlignHCenter

                    text:"取消"
                    font.bold: true
                    font.pixelSize: 15


                    onClicked: {
                        root.visable_right_side=false;
                    }
                }

                Button{
                    implicitHeight: 40
                    implicitWidth: 120

                    highlighted: true

                    Layout.alignment: Qt.AlignHCenter

                    text:"添加"
                    font.bold: true
                    font.pixelSize: 15

                    onClicked: {
                        // root.visable_right_side=false;

                        var item= CasterMonitor.genPullStreamTemp()

                        item.type           = root.type
                        item.target_ip      = root.target_ip
                        item.target_port    = root.target_port
                        item.target_mpt     = root.target_mpt
                        item.target_account = root.target_account
                        item.target_password= root.target_password
                        item.login_mpt      = root.login_mpt
                        item.UID= root.login_mpt

                        console.log(Util.safeStringify(item))


                        root.addTaskOpUid= CasterMonitor.addAddPullStreamOperate(item)

                        CasterMonitor.excuteOperate(addTaskOpUid)

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
