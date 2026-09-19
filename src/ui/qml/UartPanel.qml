import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

PanelCard {
    title: "USART console"
    badge: "USART2"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6
        TextArea {
            Layout.fillWidth: true
            Layout.fillHeight: true
            readOnly: true
            wrapMode: TextEdit.Wrap
            selectByMouse: true
            font.family: Theme.monoFont
            font.pixelSize: 12
            text: uartModel.text
            color: Theme.codeGreen
            selectedTextColor: Theme.bg
            selectionColor: Theme.accent
            background: Rectangle { color: Theme.panelAlt; radius: 6; border.color: Theme.border }
        }
        RowLayout {
            spacing: 6
            TextField {
                id: input
                Layout.fillWidth: true
                implicitHeight: 30
                placeholderText: "send to MCU RX…"
                font.family: Theme.monoFont
                color: Theme.text
                leftPadding: 8
                background: Rectangle {
                    radius: 6
                    color: Theme.panelAlt
                    border.color: parent.activeFocus ? Theme.accent : Theme.border
                }
                onAccepted: {
                    uartModel.send(text)
                    text = ""
                }
            }
            AppButton { text: "Clear"; onClicked: uartModel.clear() }
        }
    }
}
