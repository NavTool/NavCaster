import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import FluentUI.Controls
import FluentUI.impl
import CasterMonitor

Item{

    id:root

    property var initialItem

    anchors.fill:parent

    InfoBarManager{
        id: tip_topright
        target: root
        edge: Qt.TopEdge | Qt.RightEdge
    }

    InfoBarManager{
        id: tip_top
        target: root
        edge: Qt.TopEdge
        isOverlay:true   //置顶，放置在最前端
    }

    InfoBarManager{
        id: tip_topleft
        target: root
        edge: Qt.TopEdge | Qt.LeftEdge
    }

    InfoBarManager{
        id: tip_bottomright
        target: root
        edge: Qt.BottomEdge | Qt.RightEdge
    }

    InfoBarManager{
        id: tip_bottom
        target: root
        edge: Qt.BottomEdge
    }

    InfoBarManager{
        id: tip_bottomleft
        target: root
        edge: Qt.BottomEdge | Qt.LeftEdge
    }

    PageRouter{
        id: screen_router
        routes: {
            "/screen/init": R.resolvedUrl("qml/screen/Screen_Init.qml"),
            "/screen/main": R.resolvedUrl("qml/screen/Screen_Main.qml"),
        }
    }


    PageRouter{
        id: dialog_router
        routes: {
            "/test/dialog/test/sinopvt": R.resolvedUrl("qml/dialog/TEST/Dialog_Test_SinoPvt.qml"),

            "/717/dialog/717/edit_merge": R.resolvedUrl("qml/dialog/717/Dialog_717_EditMerge.qml"),
            "/717/dialog/717/edit_obs": R.resolvedUrl("qml/dialog/717/Dialog_717_EditObs.qml"),
            "/717/dialog/717/edit_output": R.resolvedUrl("qml/dialog/717/Dialog_717_EditOutput.qml"),
            "/717/dialog/717/edit_process": R.resolvedUrl("qml/dialog/717/Dialog_717_EditProcess.qml"),
            "/717/dialog/717/export_sol": R.resolvedUrl("qml/dialog/717/Dialog_717_ExportSol.qml"),
            "/717/dialog/717/import_dynamicobs": R.resolvedUrl("qml/dialog/717/Dialog_717_ImportDynamicObs.qml"),
            "/717/dialog/717/import_staticobs": R.resolvedUrl("qml/dialog/717/Dialog_717_ImportStaticObs.qml"),

            "/gnss/dialog/analyze/data_qc": R.resolvedUrl("qml/dialog/GNSS/Dialog_Analyze_DataQC.qml"),
            "/gnss/dialog/analyze/display_report": R.resolvedUrl("qml/dialog/GNSS/Dialog_Analyze_DiaplayReport.qml"),
            "/gnss/dialog/analyze/edit_option": R.resolvedUrl("qml/dialog/GNSS/Dialog_Analyze_EditOption.qml"),
            "/gnss/dialog/analyze/export_qc": R.resolvedUrl("qml/dialog/GNSS/Dialog_Analyze_ExportQc.qml"),
            "/gnss/dialog/analyze/export_report": R.resolvedUrl("qml/dialog/GNSS/Dialog_Analyze_ExportReport.qml"),
            "/gnss/dialog/analyze/proc_analyze": R.resolvedUrl("qml/dialog/GNSS/Dialog_Analyze_ProcAnalyze.qml"),

            "/gnss/dialog/baseline/edit_option": R.resolvedUrl("qml/dialog/GNSS/Dialog_Baseline_EditOption.qml"),
            "/gnss/dialog/baseline/proc_baseline": R.resolvedUrl("qml/dialog/GNSS/Dialog_Baseline_ProcBaseline.qml"),
            "/gnss/dialog/baseline/search_loop": R.resolvedUrl("qml/dialog/GNSS/Dialog_Baseline_SearchLoop.qml"),

            "/gnss/dialog/convert/conv_coord": R.resolvedUrl("qml/dialog/GNSS/Dialog_Convert_ConvCoord.qml"),
            "/gnss/dialog/convert/conv_rinex": R.resolvedUrl("qml/dialog/GNSS/Dialog_Convert_ConvRinex.qml"),
            "/gnss/dialog/convert/eidt_option": R.resolvedUrl("qml/dialog/GNSS/Dialog_Convert_EditOption.qml"),
            "/gnss/dialog/convert/merge_file": R.resolvedUrl("qml/dialog/GNSS/Dialog_Convert_MergeFile.qml"),
            "/gnss/dialog/convert/filter_file": R.resolvedUrl("qml/dialog/GNSS/Dialog_Convert_FilterFile.qml"),
            "/gnss/dialog/convert/split_file": R.resolvedUrl("qml/dialog/GNSS/Dialog_Convert_SplitFile.qml"),

            "/gnss/dialog/data/edit_option": R.resolvedUrl("qml/dialog/GNSS/Dialog_Data_EditOption.qml"),

            "/gnss/dialog/data/batch_import": R.resolvedUrl("qml/dialog/GNSS/Dialog_Data_BatchImport.qml"),
            "/gnss/dialog/data/import_nav": R.resolvedUrl("qml/dialog/GNSS/Dialog_Data_ImportNav.qml"),
            "/gnss/dialog/data/import_obs": R.resolvedUrl("qml/dialog/GNSS/Dialog_Data_ImportObs.qml"),
            "/gnss/dialog/data/import_qc": R.resolvedUrl("qml/dialog/GNSS/Dialog_Data_ImportQc.qml"),
            "/gnss/dialog/data/import_sp3": R.resolvedUrl("qml/dialog/GNSS/Dialog_Data_ImportSp3.qml"),
            "/gnss/dialog/data/import_sol": R.resolvedUrl("qml/dialog/GNSS/Dialog_Data_ImportSol.qml"),
            "/gnss/dialog/data/manage_list": R.resolvedUrl("qml/dialog/GNSS/Dialog_Data_ManageList.qml"),


            "/gnss/dialog/gnss/proc_ppp": R.resolvedUrl("qml/dialog/GNSS/Dialog_GNSS_ProcPPP.qml"),
            "/gnss/dialog/gnss/proc_pvt": R.resolvedUrl("qml/dialog/GNSS/Dialog_GNSS_ProcPVT.qml"),
            "/gnss/dialog/gnss/proc_rtk": R.resolvedUrl("qml/dialog/GNSS/Dialog_GNSS_ProcRTK.qml"),
            "/gnss/dialog/gnss/proc_spp": R.resolvedUrl("qml/dialog/GNSS/Dialog_GNSS_ProcSPP.qml"),
            "/gnss/dialog/gnss/proc_stic": R.resolvedUrl("qml/dialog/GNSS/Dialog_GNSS_ProcSTIC.qml"),
            "/gnss/dialog/gnss/edit_option": R.resolvedUrl("qml/dialog/GNSS/Dialog_GNSS_EditOption.qml"),



            "/gnss/dialog/netadjust/edit_option": R.resolvedUrl("qml/dialog/GNSS/Dialog_NetAdjust_EditOption.qml"),
            "/gnss/dialog/netadjust/export_report": R.resolvedUrl("qml/dialog/GNSS/Dialog_NetAdjust_ExportReport.qml"),
            "/gnss/dialog/netadjust/proc_adj": R.resolvedUrl("qml/dialog/GNSS/Dialog_NetAdjust_ProcADJ.qml"),

            "/gnss/dialog/process/add_task": R.resolvedUrl("qml/dialog/GNSS/Dialog_Process_AddTask.qml"),
            "/gnss/dialog/process/edit_option": R.resolvedUrl("qml/dialog/GNSS/Dialog_Process_EditOption.qml"),
            "/gnss/dialog/process/manage_templete": R.resolvedUrl("qml/GNSS/dialog/Dialog_Process_ManageTemplete.qml"),


            "/gnss/dialog/report/edit_option": R.resolvedUrl("qml/dialog/GNSS/Dialog_Report_EditOption.qml"),
            "/gnss/dialog/report/export_report": R.resolvedUrl("qml/dialog/GNSS/Dialog_Report_ExportReport.qml"),

            "/gnss/dialog/solution/export_sol": R.resolvedUrl("qml/dialog/GNSS/Dialog_Solution_ExportSol.qml"),

            "/gnss/dialog/station/add_new": R.resolvedUrl("qml/dialog/GNSS/Dialog_Station_AddNew.qml"),
            "/gnss/dialog/station/edit_property": R.resolvedUrl("qml/dialog/GNSS/Dialog_Station_EditProperty.qml"),
            "/gnss/dialog/station/manage_exist": R.resolvedUrl("qml/dialog/GNSS/Dialog_Station_ManageExist.qml"),

            "/ins/dialog/data/import_imu": R.resolvedUrl("qml/dialog/INS/Dialog_Data_ImportIMU.qml"),
            "/ins/dialog/ins/edit_option": R.resolvedUrl("qml/dialog/INS/Dialog_INS_EditOption.qml"),
            "/ins/dialog/ins/export_res": R.resolvedUrl("qml/dialog/INS/Dialog_INS_ExportRes.qml"),
            "/ins/dialog/ins/proc_lc": R.resolvedUrl("qml/dialog/INS/Dialog_INS_ProcLC.qml"),


            "/dialog/project/calculate_para": R.resolvedUrl("qml/dialog/Dialog_Project_CalculatePara.qml"),
            "/dialog/project/edit_property": R.resolvedUrl("qml/dialog/Dialog_Project_EditProperty.qml"),
            "/dialog/project/edit_survey": R.resolvedUrl("qml/dialog/Dialog_Project_EditSurvey.qml"),
            "/dialog/project/export_project": R.resolvedUrl("qml/dialog/Dialog_Project_ExportProject.qml"),
            "/dialog/project/new_project": R.resolvedUrl("qml/dialog/Dialog_Project_NewProject.qml"),
            "/dialog/project/open_project": R.resolvedUrl("qml/dialog/Dialog_Project_OpenPorject.qml"),
            "/dialog/project/save_as": R.resolvedUrl("qml/dialog/Dialog_Project_SaveAs.qml"),


            "/dialog/system/edit_antenna": R.resolvedUrl("qml/dialog/Dialog_System_EditAntenna.qml"),
            "/dialog/system/edit_coord": R.resolvedUrl("qml/dialog/Dialog_System_EditCoord.qml"),
            "/dialog/system/edit_option": R.resolvedUrl("qml/dialog/Dialog_System_EditOption.qml"),
            "/dialog/system/edit_process": R.resolvedUrl("qml/dialog/Dialog_System_EditProcess.qml"),
            "/dialog/system/edit_standard": R.resolvedUrl("qml/dialog/Dialog_System_EditStandard.qml"),

            "/dialog/system/show_about": R.resolvedUrl("qml/dialog/Dialog_System_ShowAbout.qml"),
            "/dialog/system/show_help": R.resolvedUrl("qml/dialog/Dialog_System_ShowHelp.qml"),
            "/dialog/system/show_version": R.resolvedUrl("qml/dialog/Dialog_System_ShowVersion.qml")


        }
    }




    // 用于存储多个对话框的Loader
    HotLoader {
        id: dialogLoader

        // property string title
        // property var path
        // property var url
        // property var argument
        // property bool singleton
        // property var stackView: control
        // property var pageRouter: control.router
        // property alias __context: context
        // property var __current:{
        //     if(singleton){
        //         return url
        //     }
        //     return visible ? url : ""
        // }
        // on__CurrentChanged: {
        //     dialogLoader.setSource(__current,{context:context})
        // }
        // reload: function(){
        //     var timestamp = Date.now()
        //     var url = new URL(__current)
        //     url.searchParams.set('timestamp', timestamp);
        //     dialogLoader.setSource(url.toString(),{context:context})
        // }
        // StackView.onActivated: {
        //     context.activated()
        // }
        // StackView.onDeactivated: {
        //     context.deactivated()
        // }
        // PageContext{
        //     id: context
        //     path: dialogLoader.path
        //     url: dialogLoader.url
        //     argument: dialogLoader.argument
        //     singleton: dialogLoader.singleton
        //     view: dialogLoader.stackView
        //     router: dialogLoader.pageRouter
        // }
        // onLoaded: {
        //     dialogLoader.title = loader_panne.item.title
        // }

    }

    Connections{
        target: Global

        // function onOpen_dialog(path)
        // {
        //     if(dialogLoader.source===dialog_router.toUrl(path))
        //     {
        //         console.log("reload dialog")
        //         dialogLoader.reload()
        //     }
        //     else
        //     {
        //         console.log("open dialog:"+path)
        //         dialogLoader.source = dialog_router.toUrl(path)
        //         dialogLoader.reload()
        //     }
        // }


        function onOpen_dialog(path, args) {
            let url = dialog_router.toUrl(path)
            if (dialogLoader.source === url) {
                console.log("reload dialog with args")
                dialogLoader.setSource(url, {argument: args})
            } else {
                console.log("open dialog: " + path)
                dialogLoader.setSource(url, {argument: args})
            }
        }
    }



    PageRouterView{
        id: screen_panne
        anchors.fill: parent
        router: screen_router
        clip: true

        Component.onCompleted: {
            screen_router.go(Global.displayScreen)
        }
    }

    Connections{
        target:Global
        function onDisplayScreenChanged(){
            screen_router.go(Global.displayScreen)
        }
    }
    Shortcut {
        sequence: "Ctrl+D"
        onActivated: {
            Global.debugMode = !Global.debugMode
            tip_top.showInfo(qsTr("Debug Mode:  %1").arg(Global.debugMode ? "Open" : "Close"))
        }
    }


    Connections
    {
        target: CasterMonitor

        function onNoticeSuccess(msg)
        {
            tip_top.showSuccess(msg)
        }
        function onNoticeInfo(msg)
        {
            tip_top.showInfo(msg)
        }
        function onNoticeWarning(msg)
        {
            tip_top.showWarning(msg)
        }
        function onNoticeError(msg)
        {
            tip_top.showError(msg)
        }

        function onConnectCasterSuccess()
        {
            tip_top.showSuccess("onConnectCasterSuccess")
        }

        function onConnectCasterFailed()
        {
            tip_top.showSuccess("onConnectCasterFailed")
        }

        function onReconnectCaster()
        {

        }

        function onDisconnectCaster()
        {

        }



    }





}

