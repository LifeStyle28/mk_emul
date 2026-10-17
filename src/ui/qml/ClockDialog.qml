import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Shapes 1.15

// Clock Configuration (Build menu), like the Clock Configuration tab of STM32CubeMX: the clock
// tree of the STM32F407 with the PLL, the system clock mux and the bus prescalers. Values over
// the datasheet limits are red. "emulator" lines show what the running firmware configured.
Dialog {
    id: dlg
    title: "Clock Configuration — STM32F407"
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(1250, parent.width - 24)
    height: Math.min(800, parent.height - 24)
    padding: 12
    property string resultText: ""
    onOpened: resultText = ""

    background: Rectangle {
        color: Theme.panel
        border.color: Theme.border
        radius: 8
    }
    header: Rectangle {
        height: 40
        color: Theme.header
        radius: 8
        Label {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            text: dlg.title
            color: Theme.text
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }
        Label {
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            text: clockModel.halProject ? "HAL project: HAL_RCC_OscConfig / HAL_RCC_ClockConfig"
                                        : "register-level project: RCC / FLASH / PWR registers"
            color: Theme.muted
            font.pixelSize: 11
        }
    }

    function mhz(hz) {
        if (hz === undefined || hz === 0)
            return "—"
        const v = hz / 1e6
        return (Math.round(v * 1000) / 1000) + " MHz"
    }
    function isBad(name) { return clockModel.bad.indexOf(name) >= 0 }
    readonly property var f: clockModel.freq
    readonly property var live: clockModel.live

    // ---- building blocks of the tree --------------------------------------------------
    component Box: Rectangle {
        id: box
        property string caption: ""
        property string value: ""
        property bool bad: false
        property var liveHz: undefined
        property var expectHz: undefined
        width: 120
        height: 62
        radius: 6
        color: Theme.panelAlt
        border.color: bad ? Theme.danger : Theme.border
        border.width: bad ? 2 : 1
        Column {
            anchors.centerIn: parent
            spacing: 1
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: box.caption
                color: Theme.muted
                font.pixelSize: 10
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: box.value
                color: box.bad ? Theme.danger : Theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
                font.family: Theme.monoFont
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: box.liveHz !== undefined
                text: "emulator: " + dlg.mhz(box.liveHz)
                color: box.liveHz === box.expectHz ? Theme.ok : Theme.warn
                font.pixelSize: 9
            }
        }
    }

    component Block: Rectangle {
        property string caption: ""
        radius: 6
        color: "#101820"
        border.color: Theme.accentDim
        Label {
            x: 8
            y: 4
            text: parent.caption
            color: Theme.accent
            font.pixelSize: 10
            font.weight: Font.DemiBold
        }
    }

    component Wire: Shape {
        property var points: []
        property bool active: true
        anchors.fill: parent
        ShapePath {
            strokeColor: active ? Theme.accent : "#33404f"
            strokeWidth: active ? 2 : 1.5
            fillColor: "transparent"
            PathPolyline { path: points }
        }
    }

    component Spin: SpinBox {
        editable: true
        implicitHeight: 28
        implicitWidth: 92
        font.pixelSize: 12
        font.family: Theme.monoFont
    }

    component Combo: ComboBox {
        property string prefix: "/"
        implicitHeight: 28
        implicitWidth: 76
        font.pixelSize: 12
        font.family: Theme.monoFont
        displayText: prefix + currentText
    }

    contentItem: ColumnLayout {
        spacing: 8

        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: "Clock tree" }
            TabButton { text: "Generated code" }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            Flickable {
                clip: true
                contentWidth: canvas.width
                contentHeight: canvas.height
                ScrollBar.horizontal: ScrollBar {}
                ScrollBar.vertical: ScrollBar {}

                Item {
                    id: canvas
                    width: 1200
                    height: 480

                    readonly property bool pll: dlg.f.pllUsed
                    readonly property int sw: clockModel.sysclkSource

                    // Wires first: the boxes are drawn over their ends.
                    Wire { points: [Qt.point(140, 50), Qt.point(680, 50)]; active: canvas.sw === 0 }
                    Wire { points: [Qt.point(140, 50), Qt.point(158, 50), Qt.point(158, 175), Qt.point(175, 175)]; active: canvas.pll && clockModel.pllSource === 0 }
                    Wire { points: [Qt.point(140, 335), Qt.point(158, 335), Qt.point(158, 205), Qt.point(175, 205)]; active: canvas.pll && clockModel.pllSource === 1 }
                    Wire { points: [Qt.point(140, 345), Qt.point(150, 345), Qt.point(150, 405), Qt.point(648, 405), Qt.point(648, 82), Qt.point(680, 82)]; active: canvas.sw === 1 }
                    Wire { points: [Qt.point(275, 190), Qt.point(305, 190)]; active: canvas.pll }
                    Wire { points: [Qt.point(395, 190), Qt.point(425, 190)]; active: canvas.pll }
                    Wire { points: [Qt.point(635, 175), Qt.point(668, 175), Qt.point(668, 112), Qt.point(680, 112)]; active: canvas.pll }
                    Wire { points: [Qt.point(635, 290), Qt.point(660, 290), Qt.point(660, 320), Qt.point(680, 320)]; active: canvas.pll }
                    Wire { points: [Qt.point(800, 76), Qt.point(965, 76)] }
                    Wire { points: [Qt.point(1055, 76), Qt.point(1075, 76)] }
                    Wire { points: [Qt.point(1062, 76), Qt.point(1062, 120), Qt.point(950, 120), Qt.point(950, 335), Qt.point(965, 335)] }
                    Wire { points: [Qt.point(950, 190), Qt.point(965, 190)] }
                    Wire { points: [Qt.point(1055, 190), Qt.point(1075, 190)] }
                    Wire { points: [Qt.point(1064, 190), Qt.point(1064, 255), Qt.point(1075, 255)] }
                    Wire { points: [Qt.point(1055, 335), Qt.point(1075, 335)] }
                    Wire { points: [Qt.point(1064, 335), Qt.point(1064, 400), Qt.point(1075, 400)] }

                    // Sources
                    Box { x: 10; y: 20; width: 130; caption: "HSI RC oscillator"; value: "16 MHz" }
                    Box { x: 10; y: 305; width: 130; caption: "HSE crystal (Discovery)"; value: "8 MHz" }

                    // PLL source mux
                    Block {
                        x: 175; y: 140; width: 100; height: 100
                        caption: "PLL Source"
                        Column {
                            x: 6; y: 22
                            RadioButton { text: "HSI"; checked: clockModel.pllSource === 0; onClicked: clockModel.pllSource = 0; font.pixelSize: 11 }
                            RadioButton { text: "HSE"; checked: clockModel.pllSource === 1; onClicked: clockModel.pllSource = 1; font.pixelSize: 11 }
                        }
                    }

                    // /M
                    Block {
                        x: 305; y: 150; width: 90; height: 84
                        caption: "/M"
                        border.color: dlg.isBad("vcoIn") || dlg.isBad("pllM") ? Theme.danger : Theme.accentDim
                        Spin {
                            x: 4; y: 22; width: 82
                            from: 2; to: 63
                            value: clockModel.pllM
                            onValueModified: clockModel.pllM = value
                        }
                        Label {
                            x: 6; y: 56
                            text: "in " + dlg.mhz(dlg.f.vcoIn)
                            color: dlg.isBad("vcoIn") ? Theme.danger : Theme.muted
                            font.pixelSize: 10
                        }
                    }

                    // Main PLL
                    Block {
                        x: 425; y: 105; width: 210; height: 220
                        caption: "Main PLL"
                        GridLayout {
                            x: 10; y: 24
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 6
                            Label { text: "×N"; color: Theme.text; font.pixelSize: 12 }
                            Spin {
                                from: 50; to: 432
                                value: clockModel.pllN
                                onValueModified: clockModel.pllN = value
                            }
                            Label { text: "VCO"; color: Theme.muted; font.pixelSize: 11 }
                            Label {
                                text: dlg.mhz(dlg.f.vcoOut)
                                color: dlg.isBad("vcoOut") ? Theme.danger : Theme.text
                                font.family: Theme.monoFont
                                font.pixelSize: 12
                            }
                            Label { text: "/P"; color: Theme.text; font.pixelSize: 12 }
                            Combo {
                                model: [2, 4, 6, 8]
                                currentIndex: [2, 4, 6, 8].indexOf(clockModel.pllP)
                                onActivated: function(i) { clockModel.pllP = [2, 4, 6, 8][i] }
                            }
                            Label { text: "PLLCLK"; color: Theme.muted; font.pixelSize: 11 }
                            Label {
                                text: dlg.mhz(dlg.f.pllClk)
                                color: dlg.isBad("pllClk") ? Theme.danger : Theme.text
                                font.family: Theme.monoFont
                                font.pixelSize: 12
                            }
                            Label { text: "/Q"; color: Theme.text; font.pixelSize: 12 }
                            Spin {
                                from: 2; to: 15
                                value: clockModel.pllQ
                                onValueModified: clockModel.pllQ = value
                            }
                        }
                    }

                    // System clock mux
                    Block {
                        x: 680; y: 20; width: 120; height: 120
                        caption: "System Clock Mux"
                        Column {
                            x: 6; y: 20
                            RadioButton { text: "HSI"; checked: clockModel.sysclkSource === 0; onClicked: clockModel.sysclkSource = 0; font.pixelSize: 11 }
                            RadioButton { text: "HSE"; checked: clockModel.sysclkSource === 1; onClicked: clockModel.sysclkSource = 1; font.pixelSize: 11 }
                            RadioButton { text: "PLLCLK"; checked: clockModel.sysclkSource === 2; onClicked: clockModel.sysclkSource = 2; font.pixelSize: 11 }
                        }
                    }

                    Box {
                        x: 820; y: 40; width: 120; height: 72
                        caption: "SYSCLK (max 168)"
                        value: dlg.mhz(dlg.f.sysclk)
                        bad: dlg.isBad("sysclk")
                        liveHz: dlg.live.sysclk
                        expectHz: dlg.f.sysclk
                    }

                    Block {
                        x: 965; y: 44; width: 90; height: 64
                        caption: "AHB Prescaler"
                        Combo {
                            x: 6; y: 26; width: 78
                            model: [1, 2, 4, 8, 16, 64, 128, 256, 512]
                            currentIndex: [1, 2, 4, 8, 16, 64, 128, 256, 512].indexOf(clockModel.ahbDiv)
                            onActivated: function(i) { clockModel.ahbDiv = [1, 2, 4, 8, 16, 64, 128, 256, 512][i] }
                        }
                    }

                    Box {
                        x: 1075; y: 40; width: 120; height: 72
                        caption: "HCLK: core, AHB (168)"
                        value: dlg.mhz(dlg.f.hclk)
                        bad: dlg.isBad("hclk")
                        liveHz: dlg.live.hclk
                        expectHz: dlg.f.hclk
                    }

                    Box {
                        x: 820; y: 155; width: 120; height: 62
                        caption: "Flash latency"
                        value: clockModel.flashLatency + " WS"
                    }

                    Block {
                        x: 965; y: 158; width: 90; height: 64
                        caption: "APB1 Prescaler"
                        Combo {
                            x: 6; y: 26; width: 78
                            model: [1, 2, 4, 8, 16]
                            currentIndex: [1, 2, 4, 8, 16].indexOf(clockModel.apb1Div)
                            onActivated: function(i) { clockModel.apb1Div = [1, 2, 4, 8, 16][i] }
                        }
                    }
                    Box {
                        x: 1075; y: 152; width: 120; height: 72
                        caption: "APB1 periph (max 42)"
                        value: dlg.mhz(dlg.f.pclk1)
                        bad: dlg.isBad("pclk1")
                        liveHz: dlg.live.pclk1
                        expectHz: dlg.f.pclk1
                    }
                    Box {
                        x: 1075; y: 230; width: 120; height: 56
                        caption: "APB1 timers (TIM2-7)"
                        value: dlg.mhz(dlg.f.tim1)
                        liveHz: dlg.live.tim1
                        expectHz: dlg.f.tim1
                    }

                    Block {
                        x: 965; y: 303; width: 90; height: 64
                        caption: "APB2 Prescaler"
                        Combo {
                            x: 6; y: 26; width: 78
                            model: [1, 2, 4, 8, 16]
                            currentIndex: [1, 2, 4, 8, 16].indexOf(clockModel.apb2Div)
                            onActivated: function(i) { clockModel.apb2Div = [1, 2, 4, 8, 16][i] }
                        }
                    }
                    Box {
                        x: 1075; y: 297; width: 120; height: 72
                        caption: "APB2 periph (max 84)"
                        value: dlg.mhz(dlg.f.pclk2)
                        bad: dlg.isBad("pclk2")
                        liveHz: dlg.live.pclk2
                        expectHz: dlg.f.pclk2
                    }
                    Box {
                        x: 1075; y: 375; width: 120; height: 56
                        caption: "APB2 timers (TIM1, 8-11)"
                        value: dlg.mhz(dlg.f.tim2)
                        liveHz: dlg.live.tim2
                        expectHz: dlg.f.tim2
                    }

                    Box {
                        x: 680; y: 292; width: 170; height: 62
                        caption: "48 MHz clocks (USB, SDIO, RNG)"
                        value: canvas.pll ? dlg.mhz(dlg.f.pll48) : "PLL off"
                        bad: dlg.isBad("pll48")
                    }

                    Label {
                        x: 10; y: 440
                        width: 820
                        wrapMode: Text.Wrap
                        text: "Limits (STM32F407, 3.3 V): VCO input 1–2 MHz, VCO output 100–432 MHz, " +
                              "SYSCLK/HCLK ≤ 168 MHz, APB1 ≤ 42 MHz, APB2 ≤ 84 MHz, 48 MHz clock ≤ 48 MHz. " +
                              "Timers run at 2×PCLK when their APB prescaler is not 1."
                        color: Theme.muted
                        font.pixelSize: 10
                    }
                }
            }

            ScrollView {
                clip: true
                TextArea {
                    readOnly: true
                    selectByMouse: true
                    text: clockModel.code
                    font.family: Theme.monoFont
                    font.pixelSize: 12
                    color: Theme.text
                    wrapMode: TextEdit.NoWrap
                    background: Rectangle { color: "#0d131a"; radius: 6; border.color: Theme.border }
                }
            }
        }

        // Problems
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Repeater {
                model: clockModel.errors
                Label { text: "✖ " + modelData; color: Theme.danger; font.pixelSize: 12 }
            }
            Repeater {
                model: clockModel.warnings
                Label { text: "⚠ " + modelData; color: Theme.warn; font.pixelSize: 12 }
            }
            Label {
                visible: clockModel.errors.length === 0 && clockModel.warnings.length === 0
                text: "✔ Configuration is within the STM32F407 limits"
                color: Theme.ok
                font.pixelSize: 12
            }
            Label {
                visible: dlg.resultText.length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: dlg.resultText
                color: Theme.accent
                font.pixelSize: 12
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label { text: "Presets:"; color: Theme.muted; font.pixelSize: 11 }
            AppButton { text: "168 MHz (HSE+PLL)"; onClicked: clockModel.preset("168") }
            AppButton { text: "84 MHz (HSI+PLL)"; onClicked: clockModel.preset("84") }
            AppButton { text: "16 MHz HSI (reset)"; onClicked: clockModel.preset("hsi16") }
            AppButton {
                text: "From Emulator"
                ToolTip.visible: hovered
                ToolTip.text: "Take the clock tree the running firmware has set up (RCC registers)"
                onClicked: clockModel.loadFromEmulator()
            }
            Item { Layout.fillWidth: true }
            AppButton { text: "Copy Code"; onClicked: { clockModel.copyCode(); dlg.resultText = "SystemClock_Config() copied to the clipboard." } }
            AppButton {
                text: "Write to Project"
                primary: true
                enabled: clockModel.errors.length === 0
                ToolTip.visible: hovered
                ToolTip.text: "Replace SystemClock_Config() in the project (or add clock_config.c)"
                onClicked: dlg.resultText = clockModel.writeToProject()
            }
            AppButton { text: "Close"; onClicked: dlg.close() }
        }
    }
}
