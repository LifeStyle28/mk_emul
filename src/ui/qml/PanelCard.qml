import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    property string title: ""
    property string badge: ""
    default property alias content: body.data
    color: Theme.panel
    radius: 8
    border.color: Theme.border
    border.width: 1
    clip: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: Theme.header
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 10
                spacing: 8
                Rectangle {
                    width: 6; height: 6; radius: 3
                    color: Theme.accent
                }
                Label {
                    text: root.title
                    color: Theme.text
                    font.family: Theme.uiFont
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }
                Label {
                    visible: root.badge.length > 0
                    text: root.badge
                    color: Theme.accent
                    font.family: Theme.monoFont
                    font.pixelSize: 10
                    background: Rectangle {
                        color: Theme.accentDim
                        radius: 4
                        anchors.fill: parent
                        anchors.margins: -4
                    }
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
        }
        Item {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
