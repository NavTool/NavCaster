import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Controls
import FluentUI.Controls
import FluentUI.impl
import CasterMonitor

Item {
    property string title
    property PageContext context
    property var argument

    id: root

    property string opUid:""

    // property string uid: "" //对于Ntrip1.0 Server账号，UID是 密码，其他的UID都是用户名

    property string account: ""
    property string password

    property int account_type      // 0 永久  1：期限（天数）  2：期限（日期） 3：时限
    property int account_state

    property int access: 0
    property int access_limit: 0
    property string access_group: ""

    property int time_valid
    property int time_limit
    property int time_expired
    // property int time_active
    // property int time_register

    property string contact_name
    property string contact_person
    property string contact_info
    property string remarks


    // property int time_modified

    property var account_info

    Component.onCompleted: {

        account_info = CasterMonitor.generateAccountRecordTemp()

        console.log(Util.safeStringify(account_info))

        contentDialog.open()
    }



    Connections {
        target: CasterMonitor

        function onOperateFinished(taskID, success, info) {
            if (taskID !== opUid) {
                return  //非当前指令,跳过
            }
            // 执行数据刷新操作

            if(success)
            {
                if(info.type==="ADD")
                {
                    tip_top.showSuccess(qsTr("账号添加完成"))
                    contentDialog.close()
                }
                else if(info.type==="ACTIVE")
                {
                    tip_top.showSuccess(qsTr("账号已激活"))
                }
            }

        }
    }

    Dialog {
        id: contentDialog

        x: Math.ceil((parent.width - width) / 2)
        y: Math.ceil((parent.height - height) / 2)
        parent: Overlay.overlay
        closePolicy: Popup.NoAutoClose //设置不自动关闭，如果设置了点击空白处这个对话框就会关闭
        modal: true
        title: qsTr("添加账号")
        standardButtons:Dialog.Cancel  ////DialogButtonBox.NoButton//
        footer:DialogButtonBox {
            Button {
                text: qsTr("添加")
                highlighted:true
                // DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole

                onClicked: {

                    var data=new Date()
                    var utc = Math.floor(data.getTime() / 1000)

                    // 填充用户信息

                    account_info.uid            = root.access===4 ? root.password:root.account
                    account_info.account        = root.access===4 ? root.password:root.account
                    account_info.password       = root.password

                    account_info.type           = root.account_type
                    account_info.state          = root.account_state

                    account_info.active         = root.access
                    account_info.connection_limit   = root.access_limit
                    account_info.group_uid   = root.access_group

                    account_info.available_days     = root.account_type  === 2 ? root.time_valid : 0
                    account_info.available_seconds     = root.account_type  === 3 ? root.time_limit : 0
                    account_info.active_time    = root.account_state === 1 ? utc : 0
                    account_info.expire_time   = root.time_expired
                    account_info.register_time  = utc

                    account_info.contact_name   = root.contact_name
                    account_info.contact_person = root.contact_person
                    account_info.contact_info   = root.contact_info

                    account_info.remark        = root.remarks

                    account_info.update_time  = utc

                    console.log(Util.safeStringify(account_info))


                    // 判断账号是否合法

                    // //创建任务


                    if(account_info.account==="")
                    {
                        confirmationDialog.open_with_msg(qsTr("信息异常"),qsTr("输入账号为空，请检查！"))
                        return
                    }
                    if(account_info.password==="")
                    {
                        confirmationDialog.open_with_msg(qsTr("信息异常"),qsTr("输入密码为空，请检查！"))
                        return
                    }
                    if(account_info.type!==0 && account_info.time_expired===0 )
                    {
                        confirmationDialog.open_with_msg(qsTr("信息异常"),qsTr("非永久账号需要输入账号过期/失效日期，请检查！"))
                        return
                    }
                    if(account_info.type===2 && account_info.available_days <= 0)
                    {
                        confirmationDialog.open_with_msg(qsTr("信息异常"),qsTr("期限账号输入有效天数异常，请检查！"))
                        return
                    }
                    if(account_info.type===3 && account_info.available_seconds <= 0)
                    {
                        confirmationDialog.open_with_msg(qsTr("信息异常"),qsTr("期限账号输入可用时长异常，请检查！"))
                        return
                    }


                    if(Global.debugMode)
                    {
                        // console.log(Util.safeStringify(data))
                        return
                    }

                    root.opUid=CasterMonitor.addAccountRecord(account_info.uid, account_info);


                    // root.task_UID=  GNSS_API.createConvRinexTask(data)
                    // var para= GNSS_API.getTaskPara(task_UID)
                    // para.taskname=qsTr("Rinex转换")
                    // console.log(Util.safeStringify(para))
                    // GNSS_API.setTaskPara(task_UID,para)

                    // convRinexDialog.startTask(task_UID)
                }
            }
        }



        width: 600
        contentHeight: 530
        Frame {
            anchors.fill: parent

            Column {

                topPadding: 20
                anchors.horizontalCenter: parent.horizontalCenter

                spacing: 10

                Row {
                    Label {
                        text: qsTr("用户信息")
                        font: Typography.subtitle
                    }
                }

                Row {
                    spacing: 10
                    Label {
                        text: qsTr("用户名/机构名:")
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    TextBox {
                        width: 400
                        placeholderText:qsTr("请输入该账号使用者的归属机构(个人/公司名/公司机构)")
                        onTextChanged:
                        {
                            root.contact_name=text
                        }
                    }
                }

                Row {
                    spacing: 10
                    Label {
                        text: qsTr("联系人:")
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    TextBox {
                        width: 185
                        placeholderText:qsTr("输入联系人姓名")
                        onTextChanged:
                        {
                            root.contact_person=text
                        }
                    }
                    Label {
                        text: qsTr("联系方式:")
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    TextBox {
                        width: 185
                        placeholderText:qsTr("输入联系方式")
                        onTextChanged:
                        {
                            root.contact_info=text
                        }
                    }
                }

                Row {
                    spacing: 10
                    Label {
                        text: qsTr("备注信息:")
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    TextBox {
                        width: 433
                        placeholderText:qsTr("请求来源、用途、及特殊情况说明等")
                        onTextChanged:
                        {
                            root.remarks=text
                        }
                    }
                }

                Row {
                    Label {
                        text: qsTr("账号配置")
                        font: Typography.subtitle
                    }
                }

                Row {
                    spacing: 10
                    Label {
                        text: "账号:"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    TextBox {
                        width: 400
                        anchors.verticalCenter: parent.verticalCenter

                        enabled: root.access!=3
                        placeholderText:qsTr("请设置账号，并点击右侧验证账号是否冲突")
                        onTextChanged:
                        {
                            root.account=text;
                        }
                        onEnabledChanged:
                        {
                            if(!enabled)
                            {
                                text=""

                                placeholderText=qsTr("不支持设置账号")
                            }
                            else
                            {
                                text=""
                                placeholderText=qsTr("请设置账号，并点击右侧验证账号是否冲突")
                            }
                        }
                    }
                    Button {
                        text: "验证"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {
                    spacing: 10

                    Label {
                        text: "密码:"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    PasswordBox {
                        width: 400
                        placeholderText: qsTr("请设置密码")
                        anchors.verticalCenter: parent.verticalCenter

                        onTextChanged:
                        {
                            root.password=text;
                        }
                    }
                    Button {
                        text: "生成"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Row {
                    spacing: 10
                    Label {
                        text: "最大支持连接数:"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    TextBox {
                        width: 100
                        anchors.verticalCenter: parent.verticalCenter

                        onTextChanged:
                        {
                            root.access_limit=text
                        }
                        Component.onCompleted:
                        {
                            text=5
                        }
                    }

                    Item {
                        height: 1
                        width: 20
                    }

                    Label {
                        text: "接入类型:"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    ComboBox {
                        width: 180
                        model: ["Ntrip Server/Client","Ntrip1.0/2.0 Client", "Ntrip1.0 Server", "Ntrip2.0 Server"]
                        anchors.verticalCenter: parent.verticalCenter

                        onCurrentIndexChanged: {

                            root.access=currentIndex+1;
                        }
                        Component.onCompleted:
                        {
                            currentIndex=1
                        }
                    }
                }

                Row {
                    spacing: 10
                    Label {
                        text: qsTr("访问权限:")
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    MultiSelectComboBox {
                        width: 420
                        anchors.verticalCenter: parent.verticalCenter
                        model: ["完全访问", "XX机构专用", "内部测试", "最近点模式", "代理模式"]
                        FluentUI.minimumHeight: 240
                        Component.onCompleted: {
                            visualModel.items.get(0).inSelected = true
                        }
                    }
                }

                Row {
                    Label {
                        text: qsTr("有效期配置")
                        font: Typography.subtitle
                    }
                }

                Row {
                    spacing: 10
                    Label {
                        text: qsTr("账号类型:")
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    ComboBox {
                        width: 335
                        model: ["永久账号", "期限账号(过期时间)", "期限账号(激活天数)", "时限账号(在线时长)"]
                        anchors.verticalCenter: parent.verticalCenter

                        onCurrentIndexChanged: {
                            root.account_type = currentIndex
                        }

                        Component.onCompleted:
                        {
                            currentIndex=0
                        }
                    }
                    CheckBox {
                        text: "立即激活"
                        anchors.verticalCenter: parent.verticalCenter


                        onCheckedChanged:
                        {
                            root.account_state=checked?1:0
                        }
                        Component.onCompleted:
                        {
                            checked=true
                        }


                    }
                }
                Row {
                    visible: root.account_type === 1

                    spacing: 10
                    Label {
                        text: "过期时间:"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    CalendarPicker {
                        showTime: true
                        anchors.verticalCenter: parent.verticalCenter

                        onCurrentChanged:
                        {
                            var utcSeconds = Math.floor(current.getTime() / 1000)
                            root.time_expired= utcSeconds
                            console.log(utcSeconds)
                        }
                    }
                }

                Row {
                    spacing: 10

                    visible: root.account_type === 2

                    Label {
                        text: qsTr("有效天数:")
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    ComboBox {

                        width: 150

                        model: ListModel {
                            id: model
                            ListElement {
                                text: "1"
                            }
                            ListElement {
                                text: "3"
                            }
                            ListElement {
                                text: "7"
                            }
                            ListElement {
                                text: "14"
                            }
                            ListElement {
                                text: "30"
                            }
                            ListElement {
                                text: "90"
                            }
                            ListElement {
                                text: "180"
                            }
                            ListElement {
                                text: "365"
                            }
                        }
                        editable: true


                        onAccepted: {
                            if (find(editText) === -1)
                            {
                                model.append({
                                                 "text": editText
                                             })

                                root.time_valid=editText
                            }

                        }

                        onEditTextChanged:
                        {
                            root.time_valid= editText
                            console.log(editText)
                        }

                        Component.onCompleted:
                        {
                            currentIndex=4
                        }
                    }
                    Label {
                        text: "天   "
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Item {
                        height: 1
                        width: 10
                    }
                    Label {
                        text: "失效时间:"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    CalendarPicker {
                        showTime: true
                        anchors.verticalCenter: parent.verticalCenter
                        onCurrentChanged:
                        {
                            var utcSeconds = Math.floor(current.getTime() / 1000)
                            root.time_expired= utcSeconds
                            console.log(utcSeconds)
                        }
                    }
                }
                Row {

                    visible: root.account_type === 3

                    spacing: 10
                    Label {
                        text: "可用时长:"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    NumberBox {
                        width: 150
                        value: 10
                        anchors.verticalCenter: parent.verticalCenter

                        onValueChanged:
                        {
                            root.time_limit=value
                        }
                        Component.onCompleted:
                        {
                            root.time_limit=value
                        }
                    }
                    Label {
                        text: "小时"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Item {
                        height: 1
                        width: 10
                    }
                    Label {
                        text: "失效时间:"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    CalendarPicker {
                        showTime: true
                        anchors.verticalCenter: parent.verticalCenter
                        onCurrentChanged:
                        {
                            var utcSeconds = Math.floor(current.getTime() / 1000)
                            root.time_expired= utcSeconds
                            console.log(utcSeconds)
                        }
                    }
                }
            }
        }
    }


    Dialog {
        id: confirmationDialog
        x: Math.ceil((parent.width - width) / 2)
        y: Math.ceil((parent.height - height) / 2)
        parent: Overlay.overlay
        modal: true
        title: qsTr("账户信息异常！")
        standardButtons: Dialog.Ok

        property string con_text

        Column {
            spacing: 20
            anchors.fill: parent
            Label {
                // id:con_text
                text: confirmationDialog.con_text
            }
        }


        function open_with_msg(title,msg)
        {
            confirmationDialog.title=title
            confirmationDialog.con_text=msg
            open()
        }
    }

}// Item{
//     property string title
//     property PageContext context
//     property var argument

//     Dialog{
//         id:contentDialog

//         x: Math.ceil((parent.width - width) / 2)
//         y: Math.ceil((parent.height - height) / 2)
//         parent: Overlay.overlay
//         closePolicy: Popup.NoAutoClose //设置不自动关闭，如果设置了点击空白处这个对话框就会关闭
//         modal: true
//         title: qsTr("未设置对话框标题")
//         standardButtons: Dialog.Yes | Dialog.No

//         width: 600
//         contentHeight: 300
//         Frame{
//             anchors.fill: parent

//             Column{
//                 anchors.centerIn: parent
//                 spacing: 10
//                 Label{
//                     text: " xxx < 闭合环边数 <  xxx"
//                 }
//             }

//         }
//         Component.onCompleted:
//         {
//             open()
//         }
//     }
// }

