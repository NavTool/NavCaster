import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import FluentUI.Controls
import FluentUI.impl
import CasterMonitor
ScrollablePage{

    title: qsTr("Test")

    Column{
        spacing: 5
        Label{
            text: qsTr("Init Screen")
            font: Typography.subtitle
        }

        Row{
            spacing: 5
            Button{
                width: 180
                height: 100

                text: qsTr("切换到Start")

                onClicked:
                {
                    Global.displayScreen= "/screen/init"
                    Global.displayInitScreen= "/init/page/start"
                }
            }

            Button{
                width: 180
                height: 100

                text: qsTr("切换到Home")

                onClicked:
                {
                    Global.displayScreen= "/screen/init"
                    Global.displayInitScreen= "/init/page/home"
                }
            }

            Button{
                width: 180
                height: 100

                text: qsTr("切换到Setting")

                onClicked:
                {
                    Global.displayScreen= "/screen/init"
                    Global.displayInitScreen= "/init/page/setting"
                }
            }
            Button{
                width: 180
                height: 100

                text: qsTr("切换到About")

                onClicked:
                {
                    Global.displayScreen= "/screen/init"
                    Global.displayInitScreen= "/init/page/about"
                }
            }

        }

        Label{
            text: qsTr("Moniton Screen")
            font: Typography.subtitle
        }

        Grid{
            width: parent.width
            spacing: 5

            columns: 5

            Button{
                width: 180
                height: 100

                text: qsTr("切换到Statue")

                onClicked:
                {
                    Global.displayScreen= "/screen/main"
                    Global.displayMainScreen= "/monitor/page/status"
                }
            }

            Button{
                width: 180
                height: 100

                text: qsTr("切换到Server")

                onClicked:
                {
                    Global.displayScreen= "/screen/main"
                    Global.displayMainScreen= "/monitor/page/server"
                }
            }

            Button{
                width: 180
                height: 100

                text: qsTr("切换到Client")

                onClicked:
                {
                    Global.displayScreen= "/screen/main"
                    Global.displayMainScreen= "/monitor/page/client"
                }
            }

            Button{
                width: 180
                height: 100

                text: qsTr("切换到User")

                onClicked:
                {
                    Global.displayScreen= "/screen/main"
                    Global.displayMainScreen= "/monitor/page/user"
                }
            }

            Button{
                width: 180
                height: 100

                text: qsTr("切换到MPT")

                onClicked:
                {
                    Global.displayScreen= "/screen/main"
                    Global.displayMainScreen= "/monitor/page/mpt"
                }
            }


            Button{
                width: 180
                height: 100

                text: qsTr("切换到Map")

                onClicked:
                {
                    Global.displayScreen= "/screen/main"
                    Global.displayMainScreen= "/monitor/page/map"
                }
            }

            Button{
                width: 180
                height: 100

                text: qsTr("切换到Event")

                onClicked:
                {
                    Global.displayScreen= "/screen/main"
                    Global.displayMainScreen= "/monitor/page/event"
                }
            }

            Button{
                width: 180
                height: 100

                text: qsTr("切换到Option")

                onClicked:
                {
                    Global.displayScreen= "/screen/main"
                    Global.displayMainScreen= "/monitor/page/option"
                }
            }

            Button{
                width: 180
                height: 100

                text: qsTr("切换到Exit")

                onClicked:
                {
                    Global.displayScreen= "/screen/main"
                    Global.displayMainScreen= "/monitor/page/exit"
                }
            }

        }


    }


}

