import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

// CPU registers and the peripheral register summary, as two tabs.
PanelCard {
    id: card
    title: "Registers"
    tabs: ["Registers", "Peripherals"]

    StackLayout {
        anchors.fill: parent
        anchors.margins: 8
        currentIndex: card.currentTab

        ListView {
            clip: true
            spacing: 1
            model: registerModel
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
                        Layout.preferredWidth: 78
                        font.family: Theme.monoFont
                        font.pixelSize: 12
                        color: Theme.codeBlue
                    }
                    Label {
                        text: value
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignRight
                        font.family: Theme.monoFont
                        font.pixelSize: 12
                        color: Theme.codeYellow
                    }
                }
            }
        }

        ListView {
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
}
