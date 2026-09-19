import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

PanelCard {
    title: "Discovery board"
    badge: "F407VG"

    Item {
        anchors.fill: parent
        anchors.margins: 10

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            Rectangle {
                id: pcb
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 10
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#1b4a36" }
                    GradientStop { position: 1.0; color: "#10281e" }
                }
                border.color: "#3d7a5c"
                border.width: 1

                // silkscreen traces
                Repeater {
                    model: 6
                    Rectangle {
                        x: 16 + index * 28
                        y: pcb.height * 0.72
                        width: 18
                        height: 2
                        color: "#c9a227"
                        opacity: 0.55
                    }
                }

                Column {
                    anchors.centerIn: parent
                    spacing: 14
                    width: parent.width - 24

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 132
                        height: 46
                        radius: 4
                        color: "#1a1a1a"
                        border.color: "#555"
                        Column {
                            anchors.centerIn: parent
                            spacing: 2
                            Text {
                                text: "STM32F407VGT6"
                                color: "#d0d0d0"
                                font.pixelSize: 11
                                font.family: Theme.monoFont
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                            Text {
                                text: "Cortex-M4  ·  168 MHz"
                                color: "#888"
                                font.pixelSize: 9
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                        }
                    }

                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 18
                        Repeater {
                            model: 4
                            Column {
                                spacing: 4
                                width: 40
                                property bool ledOn: index === 0 ? boardModel.led0
                                                     : index === 1 ? boardModel.led1
                                                     : index === 2 ? boardModel.led2 : boardModel.led3
                                property color ledColor: index === 0 ? "#58d68d"
                                                        : index === 1 ? "#f5b041"
                                                        : index === 2 ? "#e74c3c" : "#5dade2"
                                property string ledName: ["LD4", "LD3", "LD5", "LD6"][index]
                                property string ledPin: ["PD12", "PD13", "PD14", "PD15"][index]
                                Item {
                                    width: 28
                                    height: 28
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: ledOn ? 28 : 18
                                        height: width
                                        radius: width / 2
                                        color: ledOn ? ledColor : "transparent"
                                        opacity: 0.28
                                    }
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 16
                                        height: 16
                                        radius: 8
                                        color: ledOn ? ledColor : "#1a1a1a"
                                        border.color: ledOn ? "#fff6" : "#666"
                                        border.width: 1
                                    }
                                }
                                Text {
                                    text: ledName
                                    color: "#d5e8dc"
                                    font.pixelSize: 10
                                    font.bold: true
                                    anchors.horizontalCenter: parent.horizontalCenter
                                }
                                Text {
                                    text: ledPin
                                    color: "#8fb89f"
                                    font.pixelSize: 9
                                    font.family: Theme.monoFont
                                    anchors.horizontalCenter: parent.horizontalCenter
                                }
                            }
                        }
                    }
                }

                // user button
                Item {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 12
                    width: 54
                    height: 58
                    Rectangle {
                        id: userBtn
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 0
                        width: 34
                        height: 34
                        radius: 17
                        color: boardModel.button ? "#c0392b" : "#8e2a22"
                        border.color: "#e8c4c0"
                        border.width: 2
                        Rectangle {
                            anchors.centerIn: parent
                            width: 12
                            height: 12
                            radius: 6
                            color: boardModel.button ? "#f5d0cc" : "#d98880"
                        }
                    }
                    Text {
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "USER  PA0"
                        color: "#cfe3d6"
                        font.pixelSize: 9
                        font.family: Theme.monoFont
                    }
                    MouseArea {
                        anchors.fill: parent
                        onPressed: boardModel.button = true
                        onReleased: boardModel.button = false
                    }
                }
            }

            Label {
                text: "ADC PA1  ·  " + boardModel.analog
                color: Theme.muted
                font.pixelSize: 11
                font.family: Theme.monoFont
            }
            Slider {
                id: adcSlider
                Layout.fillWidth: true
                from: 0
                to: 4095
                value: boardModel.analog
                onMoved: boardModel.analog = value
                background: Rectangle {
                    x: adcSlider.leftPadding
                    y: adcSlider.topPadding + adcSlider.availableHeight / 2 - 2
                    implicitHeight: 4
                    width: adcSlider.availableWidth
                    height: 4
                    radius: 2
                    color: Theme.border
                    Rectangle {
                        width: adcSlider.visualPosition * parent.width
                        height: parent.height
                        radius: 2
                        color: Theme.accent
                    }
                }
                handle: Rectangle {
                    x: adcSlider.leftPadding + adcSlider.visualPosition * (adcSlider.availableWidth - width)
                    y: adcSlider.topPadding + adcSlider.availableHeight / 2 - height / 2
                    width: 14
                    height: 14
                    radius: 7
                    color: Theme.accent
                    border.color: "#9ff5ec"
                }
            }
        }
    }
}
