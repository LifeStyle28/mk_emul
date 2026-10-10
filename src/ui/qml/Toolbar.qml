import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

// Run / debug / build actions. Opening projects and firmware lives in the File menu (one place
// and one name for each action); the buttons here call the same functions as the menu.
ToolBar {
    id: bar
    signal openClock()
    // A file/folder dialog is open: Build & Load waits, otherwise it could build the previous
    // project before the dialog's choice arrives.
    property bool dialogOpen: false
    readonly property bool canStep: !emulator.running && (!debugSession.active || debugSession.state === "stopped")
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

    component Separator: Rectangle {
        width: 1
        height: 18
        color: Theme.border
        Layout.leftMargin: 4
        Layout.rightMargin: 4
    }

    component TipButton: AppButton {
        property string tip: ""
        ToolTip.visible: hovered && tip.length > 0
        ToolTip.delay: 500
        ToolTip.text: tip
    }

    RowLayout {
        anchors.fill: parent
        spacing: 6

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

        Separator { Layout.leftMargin: 6 }

        TipButton { text: "Reset"; tip: "Reset the MCU (Ctrl+Shift+F5)"; onClicked: emulator.reset() }
        TipButton {
            text: emulator.running ? "Pause" : "Run"
            primary: !emulator.running
            tip: emulator.running ? "Pause (Shift+F5)" : "Run / Continue (F5)"
            onClicked: emulator.running ? emulator.pause() : emulator.run()
        }
        TipButton {
            text: "Step Over"
            enabled: bar.canStep
            tip: "Next source line, calls run through (F10)"
            onClicked: emulator.stepOver()
        }
        TipButton {
            text: "Step Into"
            enabled: bar.canStep
            tip: "Next source line, enters called functions (F11)"
            onClicked: emulator.stepInto()
        }
        TipButton {
            text: "Step Out"
            enabled: bar.canStep && debugSession.active
            tip: "Run until the current function returns (Shift+F11)"
            onClicked: emulator.stepOut()
        }
        TipButton {
            text: "Step Instr"
            enabled: bar.canStep
            tip: "One machine instruction (Ctrl+F10)"
            onClicked: emulator.step()
        }

        ComboBox {
            id: speedBox
            implicitHeight: 30
            Layout.preferredWidth: 126
            font.pixelSize: 12
            textRole: "text"
            valueRole: "value"
            model: [
                { text: "Real time ×1", value: 1.0 },
                { text: "Slow ×0.1", value: 0.1 },
                { text: "Slow ×0.5", value: 0.5 },
                { text: "Fast ×2", value: 2.0 },
                { text: "Fast ×10", value: 10.0 },
                { text: "Max speed", value: 0.0 }
            ]
            Component.onCompleted: currentIndex = indexOfValue(emulator.speed)
            onActivated: emulator.speed = currentValue
            ToolTip.visible: hovered
            ToolTip.text: "Emulation speed. Real time: 1 emulated second = 1 second (SysTick delays, blinking)."
        }

        Separator {}

        TipButton {
            text: emulator.building ? "Building…" : "Build & Load"
            primary: true
            enabled: !emulator.building && !bar.dialogOpen
            tip: "Compile the project with " + emulator.optimization + ", load it and start the debugger (Ctrl+B)"
            onClicked: emulator.compileAndLoad()
        }
        ComboBox {
            id: optBox
            implicitHeight: 30
            Layout.preferredWidth: 78
            font.pixelSize: 12
            font.family: Theme.monoFont
            model: emulator.optimizationLevels
            currentIndex: emulator.optimizationLevels.indexOf(emulator.optimization)
            onActivated: emulator.optimization = currentText
            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.text: "Optimization level for Build & Load (Build → Optimization)"
        }
        TipButton {
            text: "Clock…"
            tip: "Clock configuration: PLL, AHB / APB prescalers, code for SystemClock_Config()"
            onClicked: bar.openClock()
        }

        Item { Layout.fillWidth: true }

        Label {
            // Measured emulated/real time ratio, e.g. ×1.00 in real-time mode.
            text: "speed " + emulator.measuredSpeedText
            color: Theme.muted
            font.pixelSize: 11
            font.family: Theme.monoFont
        }

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
