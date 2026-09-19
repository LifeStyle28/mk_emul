import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ToolBar {
    id: bar
    signal openFirmware()
    signal openProject()
    padding: 8
    background: Rectangle {
        color: Theme.header
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 8

        Label {
            text: "mk_emul"
            color: Theme.accent
            font.pixelSize: 15
            font.weight: Font.Bold
            leftPadding: 6
        }
        Label {
            text: "STM32F407VG"
            color: Theme.muted
            font.pixelSize: 11
            font.family: Theme.monoFont
        }

        Rectangle { width: 1; height: 18; color: Theme.border; Layout.leftMargin: 6; Layout.rightMargin: 6 }

        AppButton { text: "Reset"; onClicked: emulator.reset() }
        AppButton {
            text: emulator.running ? "Pause" : "Run"
            primary: !emulator.running
            onClicked: emulator.running ? emulator.pause() : emulator.run()
        }
        AppButton { text: "Step"; onClicked: emulator.step() }

        Rectangle { width: 1; height: 18; color: Theme.border; Layout.leftMargin: 4; Layout.rightMargin: 4 }

        AppButton { text: "Builtin LED"; onClicked: emulator.loadBuiltin() }
        AppButton { text: "Open FW…"; onClicked: bar.openFirmware() }
        AppButton { text: "Open project…"; onClicked: bar.openProject() }
        AppButton { text: "Build & Load"; primary: true; onClicked: emulator.compileAndLoad() }

        Rectangle { width: 1; height: 18; color: Theme.border; Layout.leftMargin: 4; Layout.rightMargin: 4 }

        Label { text: "BP"; color: Theme.muted; font.pixelSize: 11 }
        TextField {
            Layout.preferredWidth: 118
            implicitHeight: 30
            text: emulator.breakpointText
            font.family: Theme.monoFont
            font.pixelSize: 12
            color: Theme.text
            leftPadding: 8
            background: Rectangle {
                radius: 6
                color: Theme.panelAlt
                border.color: parent.activeFocus ? Theme.accent : Theme.border
            }
            onEditingFinished: emulator.breakpointText = text
        }
        AppButton { text: "Add"; onClicked: emulator.addBreakpoint() }
        AppButton { text: "Del"; danger: true; onClicked: emulator.removeBreakpoint() }

        Item { Layout.fillWidth: true }

        Rectangle {
            implicitHeight: 26
            implicitWidth: statusLbl.implicitWidth + 20
            radius: 13
            color: emulator.running ? "#163528" : "#3a2e14"
            border.color: emulator.running ? Theme.ok : Theme.warn
            Label {
                id: statusLbl
                anchors.centerIn: parent
                text: emulator.running ? "RUNNING" : "STOPPED"
                color: emulator.running ? Theme.ok : Theme.warn
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.family: Theme.monoFont
            }
        }
    }
}
