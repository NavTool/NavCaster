import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import FluentUI.Controls
import FluentUI.impl
import CasterMonitor
ScrollablePage{


    Column
    {


        spacing: 10
        Row{

            spacing: 10
            Frame
            {
                width: 300
                height: 180

                Label{
                    text: qsTr("运行时长")
                    anchors.centerIn: parent
                    font: Typography.subtitle
                }
            }

            Frame
            {
                width: 300
                height: 180

                Label{
                    text: qsTr("基站/基站上限")
                    anchors.centerIn: parent
                    font: Typography.subtitle
                }

            }
            Frame
            {
                width: 300
                height: 180

                Label{
                    text: qsTr("移动站/移动站上限")
                    anchors.centerIn: parent
                    font: Typography.subtitle
                }

            }
            Frame
            {
                width: 300
                height: 180


                Label{
                    text: qsTr("CPU占用")
                    anchors.centerIn: parent
                    font: Typography.subtitle
                }

            }

        }

        Frame
        {
            width: parent.width
            height: 240
            Label{
                text: qsTr("CPU占用\n基站/移动站/连接数

")
                anchors.centerIn: parent
                font: Typography.subtitle
            }


        }



    }

}

