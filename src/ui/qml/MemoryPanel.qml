import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

PanelCard {
    title: "Memory"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8
        RowLayout {
            spacing: 6
            TextField {
                Layout.fillWidth: true
                implicitHeight: 28
                text: emulator.memoryAddressText
                font.family: Theme.monoFont
                font.pixelSize: 12
                color: Theme.text
                leftPadding: 8
                background: Rectangle {
                    radius: 6
                    color: Theme.panelAlt
                    border.color: parent.activeFocus ? Theme.accent : Theme.border
                }
                onEditingFinished: emulator.memoryAddressText = text
            }
            AppButton { text: "Go"; onClicked: emulator.goMemory() }
        }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: memoryModel
            delegate: Rectangle {
                width: ListView.view.width
                height: 20
                color: index % 2 ? "transparent" : "#0c1218"
                RowLayout {
                    anchors.fill: parent
                    spacing: 8
                    Label {
                        text: addr
                        Layout.preferredWidth: 86
                        font.family: Theme.monoFont
                        font.pixelSize: 11
                        color: Theme.codeBlue
                    }
                    Label {
                        text: hex
                        Layout.fillWidth: true
                        font.family: Theme.monoFont
                        font.pixelSize: 11
                        color: Theme.text
                    }
                    Label {
                        text: ascii
                        font.family: Theme.monoFont
                        font.pixelSize: 11
                        color: Theme.codeOrange
                    }
                }
            }
        }
    }
}
