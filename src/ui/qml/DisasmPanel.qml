import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

PanelCard {
    title: "Disassembly"

    ListView {
        anchors.fill: parent
        anchors.margins: 6
        clip: true
        model: disasmModel
        delegate: Rectangle {
            width: ListView.view.width
            height: 20
            radius: 3
            color: model.current ? Theme.pcLine : (model.isBreak ? Theme.breakLine : "transparent")
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 8
                Label {
                    text: model.current ? "▸" : (model.isBreak ? "●" : " ")
                    Layout.preferredWidth: 12
                    color: model.current ? Theme.accent : Theme.danger
                    font.pixelSize: 10
                }
                Label {
                    text: model.addr
                    Layout.preferredWidth: 88
                    font.family: Theme.monoFont
                    font.pixelSize: 11
                    color: Theme.codeBlue
                }
                Label {
                    text: model.bytes
                    Layout.preferredWidth: 72
                    font.family: Theme.monoFont
                    font.pixelSize: 11
                    color: Theme.muted
                }
                Label {
                    text: model.text
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font.family: Theme.monoFont
                    font.pixelSize: 11
                    color: Theme.codeYellow
                }
            }
        }
    }
}
