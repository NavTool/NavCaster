import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import FluentUI.Controls
import FluentUI.impl
import CasterMonitor
import "../extra"

Item {
    id:root

    anchors.fill: parent

    property string title
    property PageContext context


    GroupBox{
        id:function_box
        anchors.fill: parent

        padding: 30
        Column{
            anchors{
                fill: parent
                leftMargin: 20
            }

            Item{
                width:root.width
                height: root.height*0.25
                Column{
                    spacing: 10

                    Label{
                        text: "工程已创建成功！"
                        font:Typography.title
                    }

                    Label{
                        text: "接下来，可以开始导入文件进行处理。"
                        font:Typography.bodyLarge
                    }
                }
            }
            Item{
                width:root.width
                height: root.height*0.6
                Column{
                    spacing: 5
                    Label{
                        text: "帮助"
                        font:Typography.title
                    }
                    Label{
                        text: "如果需要帮助，你可以选择以下操作："
                        font:Typography.bodyLarge
                    }
                    HyperlinkButton{
                        text: "启动界面引导"
                        font:Typography.bodystrong
                        onClicked: {
                            //Qt.openUrlExternally(text)
                        }
                    }
                    HyperlinkButton{
                        text: "查看使用说明"
                        font:Typography.bodystrong
                        onClicked: {
                            //Qt.openUrlExternally(text)
                        }
                    }
                    HyperlinkButton{
                        text: "访问官方网站"
                        font:Typography.bodystrong
                        onClicked: {
                            //Qt.openUrlExternally(text)
                        }
                    }
                    HyperlinkButton{
                        text: "提交反馈"
                        font:Typography.bodystrong
                        onClicked: {
                            //Qt.openUrlExternally(text)
                        }
                    }
                }
            }
        }
    }
}
