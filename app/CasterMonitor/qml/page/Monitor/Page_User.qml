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

    Component.onCompleted: {
        userData.loadData("")
    }

    UserDataController {
        id: userData
        onLoadDataStart: {
            panel_loading.visible = true
        }
        onLoadDataSuccess: {
            dataModel.sourceData = data
            panel_loading.visible = false
            console.log("dataModel.count: " + Util.safeStringify(
                            dataModel.count))
        }
    }

    DataGridModel {
        id: dataModel
    }

    Timer {
        id: data_refresh_timer
        repeat: true
        interval: 500
        onTriggered: {
            GnssResourceController.updateStationData()
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
                            title: qsTr("账号注册")
                            MenuItem{
                                // icon.name:  FluentIcons.graph_Info
                                text:qsTr("期限账号注册")
                                onTriggered:{
                                }
                            }
                            MenuItem{
                                // icon.name:  FluentIcons.graph_Info
                                text:qsTr("永久账号注册")
                                onTriggered:{
                                }
                            }

                            MenuSeparator { }

                            MenuItem{
                                // icon.name:  FluentIcons.graph_Info
                                text:qsTr("机构账号注册")
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

                    Pane {
                        id: panel_loading
                        anchors.fill: dataGrid
                        ProgressRing {
                            anchors.centerIn: parent
                            indeterminate: true
                        }
                        background: Rectangle {
                            color: Theme.res.solidBackgroundFillColorBase
                        }
                    }

                    defaultHeight: 30
                    defaultminimumHeight: 25
                    defaultmaximumHeight: 240
                    horizonalHeaderHeight: 30

                    sourceModel: dataModel
                    onRowClicked: model => {// console.debug(model.station_name)
                                  }
                    onRowRightClicked: model => {// console.debug(model.station_name)
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
                            title: qsTr("账号")
                            dataIndex: "station_name"
                            width: 200
                            frozen: true
                        }
                        ListElement {
                            title: qsTr("密码")
                            dataIndex: "station_name"
                            width: 200
                            // frozen: true
                        }
                        ListElement {
                            title: qsTr("账号类型（期限/永久/机构）")
                            dataIndex: "station_name"
                            width: 200
                            // frozen: true
                        }
                        ListElement{
                            title: qsTr("账号状态（启用/停用/过期）")
                            dataIndex: "coord_UID"
                            width: 200
                        }
                        ListElement{
                            title: qsTr("接入类型(基站/移动站)")
                            dataIndex: "coord_UID"
                            width: 180
                        }
                        ListElement{
                            title: qsTr("注册日期")
                            dataIndex: "coord_UID"
                            width: 100
                        }
                        ListElement{
                            title: qsTr("激活日期")
                            dataIndex: "coord_UID"
                            width: 120
                        }
                        ListElement{
                            title: qsTr("失效日期")
                            dataIndex: "coord_UID"
                            width: 120
                        }
                        ListElement{
                            title: qsTr("连接数量")
                            dataIndex: "coord_UID"
                            width: 120
                        }
                        ListElement{
                            title: qsTr("用户名/机构名")
                            dataIndex: "coord_UID"
                            width: 120
                        }
                        ListElement{
                            title: qsTr("联系人")
                            dataIndex: "coord_UID"
                            width: 120
                        }
                        ListElement{
                            title: qsTr("联系方式")
                            dataIndex: "coord_UID"
                            width: 120
                        }
                        ListElement{
                            title: qsTr("记录修改日期")
                            dataIndex: "coord_UID"
                            width: 120
                        }
                        ListElement{
                            title: qsTr("管理员ID")
                            dataIndex: "coord_UID"
                            width: 120
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
                        property_value: (GNSS_API.getStation(GNSS.focusObsFile.station_UID)).station_name
                    }
                    ComItem{
                        property_key:qsTr("挂载点")
                        property_value:GNSS.focusObsFile.file_path
                    }
                    ComItem{
                        property_key:qsTr("在线时长")
                        property_value:GNSS.focusObsFile.file_name
                    }
                    ComItem{
                        property_key:qsTr("经度")
                        property_value:GNSS.focusObsFile.first_time
                    }
                    ComItem{
                        property_key:qsTr("纬度")
                        property_value:GNSS.focusObsFile.obs_intv
                    }
                    ComItem{
                        property_key:qsTr("高程")
                        property_value: Display.format_stationtype(GNSS_API.getStation(GNSS.focusObsFile.station_UID).station_type)
                    }
                    ComItem{
                        property_key:qsTr("IP")
                        property_value:(GNSS_API.getNavFile(GNSS.focusObsFile.navfile_UID)).file_name
                    }
                    ComItem{
                        property_key:qsTr("端口")
                        property_value:GNSS.focusObsFile.measurement_ant_height.toFixed(4)
                    }
                    ComItem{
                        property_key:qsTr("累计接收数据")
                        property_value:GNSS.focusObsFile.ant_type
                    }
                    ComItem{
                        property_key:qsTr("累计发送数据")
                        property_value:GNSS_API.getCoord((GNSS_API.getStation(GNSS.focusObsFile.station_UID)).coord_UID).llh_lat
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

}
