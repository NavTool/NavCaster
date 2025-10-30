pragma Singleton

import QtQuick
import QtQuick.Controls
import FluentUI.Controls
import FluentUI.impl
import CasterMonitor

/*
   QML中通用的JavaScript函数
*/

QtObject {
    
    function toAbsolutePath(fileUrl) {
        var path = String(fileUrl);  // 确保是字符串


        // Remove the "file://" prefix
        if (path.startsWith("file://")) {
            path = path.replace("file://", "");

            // Windows-specific adjustment
            if (Qt.platform.os === "windows") {
                // On Windows, paths after "file://" may start with a drive letter (e.g., "C:/")
                // In this case, an additional slash might be present (e.g., "file:///C:/path")
                if (path.length > 3 && path.charAt(2) === ':') {
                    path = path.substring(1); // Remove the leading slash before "C:/"
                }
            }
        }
        return path;
    }

    function getFileDir(fileUrl)
    {
        var text=Util.toAbsolutePath(fileUrl)
        // 获取最后一个 `/` 的位置
        var lastSlash = text.lastIndexOf("/")
        // 提取目录部分（不包含末尾斜杠）
        var directory = text.substring(0, lastSlash)
        // 提取文件名部分（含扩展名）
        var filenameWithExt = text.substring(lastSlash + 1)
        // 获取最后一个 `.` 的位置，用于去掉扩展名
        var lastDot = filenameWithExt.lastIndexOf(".")
        var filename = (lastDot !== -1) ? filenameWithExt.substring(0, lastDot) : filenameWithExt

        return directory
    }

    function getFileName(fileUrl)
    {
        var text=Util.toAbsolutePath(fileUrl)
        // 获取最后一个 `/` 的位置
        var lastSlash = text.lastIndexOf("/")
        // 提取目录部分（不包含末尾斜杠）
        var directory = text.substring(0, lastSlash)
        // 提取文件名部分（含扩展名）
        var filenameWithExt = text.substring(lastSlash + 1)
        // 获取最后一个 `.` 的位置，用于去掉扩展名
        var lastDot = filenameWithExt.lastIndexOf(".")
        var filename = (lastDot !== -1) ? filenameWithExt.substring(0, lastDot) : filenameWithExt

        return filenameWithExt
    }

    function getFileNameWithoutExtension(fileUrl) {
        var text=Util.toAbsolutePath(fileUrl)
        // 获取最后一个 `/` 的位置
        var lastSlash = text.lastIndexOf("/")
        // 提取目录部分（不包含末尾斜杠）
        var directory = text.substring(0, lastSlash)
        // 提取文件名部分（含扩展名）
        var filenameWithExt = text.substring(lastSlash + 1)
        // 获取最后一个 `.` 的位置，用于去掉扩展名
        var lastDot = filenameWithExt.lastIndexOf(".")
        var filename = (lastDot !== -1) ? filenameWithExt.substring(0, lastDot) : filenameWithExt

        return filename
    }


    function openFoldedDir(Dir,isFolderDir)
    {
        var path;
        if(isFolderDir)
        {
            path=Dir
        }
        else
        {
            path=getFileDir(Dir)
        }

        var url

        if(path==="")
        {
            tip_top.show(InfoBarType.Info,qsTr("当前输出目录为空！"))
            return
        }
        if (Qt.platform.os === "windows") {
            url = "file:///" + path.replace(/\\/g, "/")
        } else {
            url = "file://" + path  // Linux/macOS 通常路径已是正斜杠
        }
        Qt.openUrlExternally(url)
    }




    function safeStringify(obj, space = 2) {
        const seen = new WeakSet();

        return JSON.stringify(obj, (key, value) => {
                                  if (typeof value === 'object' && value !== null) {
                                      // 检查循环引用
                                      if (seen.has(value)) {
                                          return; // 返回 undefined 会跳过该属性
                                      }
                                      seen.add(value);
                                  }
                                  return value;
                              }, space);
    }


    function safeNumber(text) {
        if (!text || text === "." || text === "-." ) return 0;

           text = text.trim();

           // 如果是标准数字，可以直接解析
           var num = Number(text);
           if (!isNaN(num)) return num;

           // 查找最后一个 '-' 符号（忽略开头的负号）
           var lastMinus = text.lastIndexOf('-');
           var start = 0;
           var sign = 1;

           if (text[0] === '-') {
               start = 1; // 开头负号
               sign = -1;
           }

           if (lastMinus > start) {
               // 拿 '-' 后面的数字
               var sub = text.slice(lastMinus + 1);
               num = parseFloat(sub);
               if (isNaN(num)) return 0;
               return -num; // 负号
           }

           // 最终兜底，尝试 parseFloat
           num = parseFloat(text);
           return isNaN(num) ? 0 : num;
    }


    function convertTaskState(state){
        switch (state) {
        case -1:
            return qsTr("未更新状态")
        case 0:
            return qsTr("未开始")
        case 1:
            return qsTr("队列中")
        case 2:
            return qsTr("启动中")
        case 3:
            return qsTr("运行中")
        case 4:
            return qsTr("暂停中")
        case 5:
            return qsTr("已暂停")
        case 6:
            return qsTr("待继续")
        case 7:
            return qsTr("已停止")
        case 8:
            return qsTr("已完成")
        case 9:
            return qsTr("取消中")
        case 10:
            return qsTr("已取消")
        case 11:
            return qsTr("已删除")
        default:
            return qsTr("未知状态")
        }
    }

    function convertSolType(sol){
        switch (sol) {
        case 0:
            return qsTr("UNSOLVE")
        case 1:
            return qsTr("SINGLE")
        case 2:
            return qsTr("DGPS")
        case 3:
            return qsTr("SBAS")
        case 4:
            return qsTr("FIXED")
        case 5:
            return qsTr("FLOAT")
        case 6:
            return qsTr("PPP")
        default:
            return qsTr("UNKNOWN")
        }
    }


    function convertUTCtoDate(sec)
    {
        var intSec = Math.floor(sec)  // 去掉小数部分
        var d = new Date(intSec * 1000)
        function pad(n) { return n < 10 ? "0" + n : n }

        return d.getUTCFullYear() + "-" +
                pad(d.getUTCMonth() + 1) + "-" +
                pad(d.getUTCDate()) + " " +
                pad(d.getUTCHours()) + ":" +
                pad(d.getUTCMinutes()) + ":" +
                pad(d.getUTCSeconds())
    }


    //将list model类型转换成按键名的组  用于echart绘图
    function convertToFieldMap(dataList) {
        let result = {}; // 用对象模拟 map
        for (let i = 0; i < dataList.length; ++i) {
            const item = dataList[i];
            for (const key in item) {
                if (!result[key]) {
                    result[key] = [];
                }
                result[key].push(item[key]);
            }
        }

        const keys = Object.keys(result);
        console.log("all keys", keys);  // 输出：["time", "value"]

        return result;
    }


    function convertCoordType(coord)
    {
        // [站点名]-[类型]-[来源]
        var station_name= GNSS_API.getStation(coord.station_UID).station_name
        var obsfile_name= GNSS_API.getObsFile(coord.obsfile_UID).file_name
        var vector_name= GNSS_API.getVector(coord.vector_UID).vector_name

        switch (coord.coord_type) {
        case 0:
            return qsTr("未知")
        case 1:
            return "[ "+ station_name +" ]: " + qsTr("文件头:") + obsfile_name
        case 2:
            return "[ "+ station_name +" ]: " + qsTr("概略值:") + obsfile_name
        case 3:
            return "[ "+ station_name +" ]: " + qsTr("SPP:") + obsfile_name
        case 4:
            return "[ "+ station_name +" ]: " + qsTr("PPP:") + obsfile_name
        case 5:
            return "[ "+ station_name +" ]: " + qsTr("基线解:") + vector_name
        case 6:
            return "[ "+ station_name +" ]: " + qsTr("约束网平差解:")
        case 7:
            return "[ "+ station_name +" ]: " +qsTr("自由网平差")
        case 8:
            return  qsTr("用户输入")
        case 9:
            return  qsTr("保存坐标:") +coord.coord_profile
        default:
            return qsTr("未定义")
        }
    }

}
