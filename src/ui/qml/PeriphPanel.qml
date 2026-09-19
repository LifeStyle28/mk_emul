import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

PanelCard {
    title: "Peripherals"

    ListView {
        anchors.fill: parent
        anchors.margins: 8
        clip: true
        spacing: 1
        model: periphModel
        delegate: Rectangle {
            width: ListView.view.width
            height: 22
            radius: 4
            color: index % 2 ? "transparent" : "#0c1218"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                Label {
                    text: name
                    Layout.fillWidth: true
                    font.family: Theme.monoFont
                    font.pixelSize: 11
                    color: Theme.codeGreen
                }
                Label {
                    text: value
                    font.family: Theme.monoFont
                    font.pixelSize: 11
                    color: Theme.text
                }
            }
        }
    }
}
