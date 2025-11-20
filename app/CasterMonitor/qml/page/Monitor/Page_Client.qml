import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import FluentUI.Controls
import FluentUI.impl
import CasterMonitor
import "../../extra"

Frame {
    id: root

    anchors.fill: parent

    property string title
    property PageContext context

    property var focusItem

    Component.onCompleted: {
        clientData.loadData("")

        data_refresh_timer.start()
    }

    ClientDataController {
        id: clientData
        onLoadDataStart: {
            // panel_loading.visible = true
        }
        onLoadDataSuccess: {
            dataModel.sourceData = data
            // panel_loading.visible = false
            // console.log("dataModel.count: " + Util.safeStringify(
            //                 dataModel.count))
        }
    }

    DataGridModel {
        id: dataModel
    }

    Timer {
        id: data_refresh_timer
        repeat: true
        interval: 1000
        onTriggered: {
            clientData.loadData()
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
                    anchors{
                        verticalCenter: parent.verticalCenter
                        left: parent.left
                        leftMargin: 5

                    }
                    spacing: 5
                    MenuBar {
                        id:menu_bar


                        Menu {
                            width: 140
                            title: qsTr("显示")
                            MenuItem{
                                // icon.name:  FluentIcons.graph_Info
                                text:qsTr("显示过期账户")
                                onTriggered:{
                                }
                            }
                            MenuItem{
                                // icon.name:  FluentIcons.graph_Info
                                text:qsTr("显示正常账户")
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
                    }

                }


                Row {

                    anchors{
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


                    Button{
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
                    onRowClicked: model => {// console.debug(model.station_name)
                                      // root.focusItem=model
                                      console.log(Util.safeStringify(model))

                                  }
                    onRowRightClicked: model => {// console.debug(model.station_name)
                                           operate_item_menu.open_with_ctx(model)
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
                                console.log(Util.safeStringify(operate_item_menu.ctx))
                            }
                        }
                    }

                    columnSourceModel: ListModel {
                        ListElement {
                            title: qsTr("账号ID")
                            dataIndex: "account"
                            width: 200
                        }
                        ListElement {
                            title: qsTr("账号机构")
                            dataIndex: "account"
                            width: 200
                        }
                        ListElement {
                            title: qsTr("接入挂载点")
                            dataIndex: "login_mpt"
                            width: 200
                            frozen: false
                        }
                        ListElement {
                            title: qsTr("实际挂载点")
                            dataIndex: "alias_mpt"
                            width: 200
                            frozen: false
                        }
                        ListElement {
                            title: qsTr("在线时长")
                            dataIndex: "online_seconds"
                            width: 200
                            rowDelegate:function(){return comp_time_label}
                            frozen: false
                        }
                        ListElement{
                            title: qsTr("纬度")
                            dataIndex: "ecef_x"
                            width: 150
                            frozen: false
                        }

                        ListElement{
                            title: qsTr("经度")
                            dataIndex: "ecef_y"
                            width: 150
                            frozen: false
                        }

                        ListElement{
                            title: qsTr("椭球高")
                            dataIndex: "ecef_z"
                            width: 100
                            frozen: false
                        }
                        ListElement{
                            title: qsTr("IP")
                            dataIndex: "ip"
                            width: 120
                            frozen: false
                        }
                        ListElement{
                            title: qsTr("端口")
                            dataIndex: "port"
                            width: 120
                        }

                        ListElement{
                            title: qsTr("数据更新时间")
                            dataIndex: "update_time"
                            rowDelegate:function(){return comp_date_label}
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



            Frame{
                id: page_extra

                anchors.fill: parent

                width: 300


                // Text{
                //     text: "显示文件具体信息"
                //     font: Typography.Subtitle
                //     anchors.centerIn: parent
                // }


                Column{
                    ComItem{
                        property_key:qsTr("用户名")
                        property_value:root.focusItem.account
                    }
                    ComItem{
                        property_key:qsTr("挂载点")
                        property_value:root.focusItem.account
                    }
                    ComItem{
                        property_key:qsTr("在线时长")
                        property_value:root.focusItem.account
                    }
                    ComItem{
                        property_key:qsTr("经度")
                        property_value:root.focusItem.account
                    }
                    ComItem{
                        property_key:qsTr("纬度")
                        property_value:root.focusItem.account
                    }
                    ComItem{
                        property_key:qsTr("高程")
                        property_value: root.focusItem.account
                    }
                    ComItem{
                        property_key:qsTr("IP")
                        property_value:root.focusItem.ip
                    }
                    ComItem{
                        property_key:qsTr("端口")
                        property_value:root.focusItem.port
                    }
                    ComItem{
                        property_key:qsTr("累计接收数据")
                        property_value:root.focusItem.recv_total
                    }
                    ComItem{
                        property_key:qsTr("累计发送数据")
                        property_value:root.focusItem.send_total
                    }
                }
            }





        }
    }

    component ComItem:Frame {
        id:info_item

        property string property_key:""
        property string property_value:""

        width: page_extra.width
        height: 40
        Label{
            id:info_key
            text: property_key
            font:Typography.bodyStrong
            elide:Text.ElideRight
            anchors{
                left: parent.left
                leftMargin: 20
                verticalCenter: parent.verticalCenter
            }
            width: page_extra.width*0.3
        }

        Rectangle {
            id:info_seg
            implicitWidth: 1
            implicitHeight: parent.height
            color: Theme.dark ? Qt.rgba(60/255,60/255,60/255,1) : Qt.rgba(210/255,210/255,210/255,1)

            anchors{
                left: info_key.right
            }
        }

        Label{
            text: property_value
            elide:Text.ElideRight
            font: Typography.bodyStrong
            anchors{
                left: info_seg.right
                leftMargin: 20
                right: parent.right
                verticalCenter: parent.verticalCenter
            }
        }
    }

    component DataItem:Item{
        property string itemtext;
        Label{
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            anchors{
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
        id: comp_date_label
        Item{
            Label{
                anchors.centerIn: parent
                text: getLocalTime(display) // 传入 UTC 秒数
                function getLocalTime(utcSeconds) {
                    if (utcSeconds === 0) {
                        return "-"
                    }

                    var localDate = new Date(utcSeconds); // 注意：如果 utcSeconds 是秒，应该乘以 1000
                    if (utcSeconds < 1e12) {
                        // 如果是秒，需要乘以 1000
                        localDate = new Date(utcSeconds * 1000);
                    }

                    let ms = String(localDate.getMilliseconds()).padStart(3, "0");

                    return localDate.getFullYear() + "-" +
                            String(localDate.getMonth() + 1).padStart(2, "0") + "-" +
                            String(localDate.getDate()).padStart(2, "0") + " " +
                            String(localDate.getHours()).padStart(2, "0") + ":" +
                            String(localDate.getMinutes()).padStart(2, "0") + ":" +
                            String(localDate.getSeconds()).padStart(2, "0") + "." +
                            ms;
                }
            }
        }
    }


    Component{
        id: comp_time_label
        DataItem{
            itemtext: formatTime(display) // 传入 UTC 秒数
            function formatTime(seconds) {
                var h = Math.floor(seconds / 3600);
                var m = Math.floor((seconds % 3600) / 60);
                var s = seconds % 60;

                return String(h).padStart(2, "0") + ":" +
                        String(m).padStart(2, "0") + ":" +
                        String(s).padStart(2, "0");
            }
        }
    }



}
