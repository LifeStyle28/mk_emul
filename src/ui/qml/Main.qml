import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    width: 1440
    height: 900
    visible: true
    title: "mk_emul  ·  STM32F407"
    color: Theme.bg
    font.family: Theme.uiFont

    palette {
        window: Theme.bg
        windowText: Theme.text
        base: Theme.panelAlt
        text: Theme.text
        button: Theme.panelAlt
        buttonText: Theme.text
        highlight: Theme.accentDim
        highlightedText: Theme.accent
        placeholderText: Theme.muted
        mid: Theme.border
        dark: Theme.header
    }

    FileDialog {
        id: firmwareDialog
        title: "Open firmware (ELF / HEX / BIN)"
        nameFilters: ["Firmware (*.elf *.hex *.bin)", "All files (*)"]
        onAccepted: emulator.loadFirmware(selectedFile)
    }

    FolderDialog {
        id: folderDialog
        title: "Open project folder"
        onAccepted: projectModel.openFolder(selectedFolder)
    }

    header: Toolbar {
        onOpenFirmware: firmwareDialog.open()
        onOpenProject: folderDialog.open()
    }

    Component {
        id: splitHandle
        Rectangle {
            implicitWidth: 8
            implicitHeight: 8
            color: SplitHandle.pressed ? Theme.accentDim : (SplitHandle.hovered ? "#18222e" : "transparent")
        }
    }

    SplitView {
        id: outer
        anchors.fill: parent
        anchors.margins: 8
        orientation: Qt.Horizontal
        handle: splitHandle

        SplitView {
            SplitView.preferredWidth: 340
            SplitView.minimumWidth: 280
            orientation: Qt.Vertical
            handle: splitHandle
            BoardPanel { SplitView.preferredHeight: 360; SplitView.minimumHeight: 260 }
            RegisterPanel { SplitView.fillHeight: true }
        }

        SplitView {
            SplitView.fillWidth: true
            orientation: Qt.Vertical
            handle: splitHandle

            SplitView {
                SplitView.fillHeight: true
                orientation: Qt.Horizontal
                handle: splitHandle
                EditorPanel {
                    SplitView.fillWidth: true
                    onRequestOpenFolder: folderDialog.open()
                }
                DisasmPanel { SplitView.preferredWidth: 380; SplitView.minimumWidth: 260 }
            }

            SplitView {
                SplitView.preferredHeight: 230
                SplitView.minimumHeight: 160
                orientation: Qt.Horizontal
                handle: splitHandle
                UartPanel { SplitView.fillWidth: true }
                BuildPanel { SplitView.preferredWidth: 360 }
            }
        }

        SplitView {
            SplitView.preferredWidth: 320
            SplitView.minimumWidth: 260
            orientation: Qt.Vertical
            handle: splitHandle
            MemoryPanel { SplitView.preferredHeight: 300 }
            PeriphPanel { SplitView.fillHeight: true }
        }
    }

    footer: Rectangle {
        height: 28
        color: Theme.header
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 16
            Rectangle {
                width: 8; height: 8; radius: 4
                color: emulator.running ? Theme.ok : Theme.warn
            }
            Label {
                text: emulator.status
                color: Theme.text
                font.pixelSize: 11
            }
            Rectangle { width: 1; height: 12; color: Theme.border }
            Label {
                text: "PC  " + emulator.pcText
                color: Theme.codeBlue
                font.family: Theme.monoFont
                font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
            Label {
                text: emulator.firmwarePath.length ? emulator.firmwarePath : "no firmware"
                color: Theme.muted
                font.pixelSize: 11
                elide: Text.ElideMiddle
                Layout.maximumWidth: 520
            }
        }
    }
}
