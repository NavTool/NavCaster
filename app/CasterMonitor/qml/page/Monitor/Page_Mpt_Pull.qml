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

              }

        Frame {
            id: body_extra
            clip: true
            visible: Global.visable_right_side
            implicitWidth: body.width * 0.5
            implicitHeight: body.height





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
