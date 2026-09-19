import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

PanelCard {
    title: "Build log"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        TextArea {
            id: log
            Layout.fillWidth: true
            Layout.fillHeight: true
            readOnly: true
            wrapMode: TextEdit.Wrap
            selectByMouse: true
            font.family: Theme.monoFont
            font.pixelSize: 11
            text: emulator.buildLog.length ? emulator.buildLog : "Ready. Use Build & Load to compile the editor project."
            color: emulator.buildLog.length ? Theme.text : Theme.muted
            background: Rectangle { color: Theme.panelAlt; radius: 6; border.color: Theme.border }
        }
    }
}
