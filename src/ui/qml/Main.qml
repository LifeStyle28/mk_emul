import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    width: 1440
    height: 900
    visible: true
    title: "mk_emul  ·  STM32F407" + (projectModel.projectName.length ? "  —  " + projectModel.projectName : "")
    color: Theme.bg
    font.family: Theme.uiFont

    // Editor takes the whole window (Ctrl+M or the button in the Source panel); the toolbar stays.
    property bool editorMaximized: false
    readonly property bool dialogOpen: firmwareDialog.visible || folderDialog.visible || newProjectDialog.visible
    readonly property bool canStep: !emulator.running && (!debugSession.active || debugSession.state === "stopped")

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

    // ---- actions: the menu, the toolbar and the shortcuts call these ----------------------
    function newProject() { newProjectDialog.open() }
    function openProject() { folderDialog.open() }
    function openFirmware() { firmwareDialog.open() }
    function buildAndLoad() {
        if (!emulator.building && !root.dialogOpen)
            emulator.compileAndLoad()
    }
    function runOrContinue() {
        if (!emulator.running)
            emulator.run()
    }
    function pause() {
        if (emulator.running)
            emulator.pause()
    }
    function toggleEditor() { root.editorMaximized = !root.editorMaximized }

    FileDialog {
        id: firmwareDialog
        title: "Open Firmware (ELF / HEX / BIN)"
        nameFilters: ["Firmware (*.elf *.hex *.bin)", "All files (*)"]
        onAccepted: emulator.loadFirmware(selectedFile)
    }

    FolderDialog {
        id: folderDialog
        title: "Open Project (folder with .c / .h files)"
        onAccepted: projectModel.openFolder(selectedFolder)
    }

    FolderDialog {
        id: newProjectDialog
        title: "New Project: choose an empty folder"
        onAccepted: projectModel.newProject(selectedFolder)
    }

    ClockDialog { id: clockDialog }

    // Breakpoints belong to the project's files.
    Connections {
        target: projectModel
        function onProjectPathChanged() { debugSession.clearBreakpoints() }
    }

    // ---- menu --------------------------------------------------------------------------
    component AppMenuItem: MenuItem {
        id: item
        property string shortcut: ""
        implicitHeight: 28
        leftPadding: 26
        rightPadding: 18
        font.pixelSize: 12
        contentItem: RowLayout {
            spacing: 28
            Label {
                text: item.text
                color: item.enabled ? Theme.text : Theme.muted
                font: item.font
                Layout.fillWidth: true
            }
            Label {
                visible: item.shortcut.length > 0
                text: item.shortcut
                color: Theme.muted
                font.pixelSize: 11
            }
        }
        indicator: Label {
            x: 9
            anchors.verticalCenter: parent.verticalCenter
            visible: item.checkable && item.checked
            text: "✓"
            color: Theme.accent
            font.pixelSize: 12
        }
        arrow: Label {
            x: item.width - width - 8
            anchors.verticalCenter: parent.verticalCenter
            visible: item.subMenu !== null
            text: "›"
            color: Theme.muted
            font.pixelSize: 14
        }
        background: Rectangle {
            color: item.highlighted && item.enabled ? Theme.accentDim : "transparent"
            radius: 4
        }
    }

    component AppMenu: Menu {
        padding: 4
        delegate: AppMenuItem {}
        background: Rectangle {
            implicitWidth: 280
            color: Theme.panel
            border.color: Theme.border
            radius: 6
        }
    }

    menuBar: MenuBar {
        background: Rectangle {
            implicitHeight: 26
            color: Theme.header
        }
        delegate: MenuBarItem {
            id: barItem
            font.pixelSize: 12
            contentItem: Label {
                text: barItem.text.replace("&", "") // Alt+F, Alt+B, ... open the menus
                color: Theme.text
                font: barItem.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: barItem.highlighted ? Theme.accentDim : "transparent"
                radius: 4
            }
        }

        AppMenu {
            title: "&File"
            AppMenuItem { text: "New Project…"; shortcut: "Ctrl+N"; onTriggered: root.newProject() }
            AppMenuItem { text: "Open Project…"; shortcut: "Ctrl+O"; onTriggered: root.openProject() }
            AppMenu {
                id: exampleMenu
                title: "Open Example"
                Instantiator {
                    model: projectModel.examples
                    delegate: AppMenuItem {
                        text: modelData
                        onTriggered: projectModel.openExample(modelData)
                    }
                    onObjectAdded: (index, object) => exampleMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => exampleMenu.removeItem(object)
                }
            }
            AppMenu {
                id: recentMenu
                title: "Recent Projects"
                enabled: projectModel.recentProjects.length > 0
                Instantiator {
                    model: projectModel.recentProjects
                    delegate: AppMenuItem {
                        text: modelData
                        onTriggered: projectModel.openPath(modelData)
                    }
                    onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => recentMenu.removeItem(object)
                }
                MenuSeparator {}
                AppMenuItem { text: "Clear Recent"; onTriggered: projectModel.clearRecentProjects() }
            }
            AppMenuItem { text: "Save All"; shortcut: "Ctrl+S"; onTriggered: projectModel.saveAll() }
            MenuSeparator {}
            AppMenuItem { text: "Open Firmware…"; shortcut: "Ctrl+Shift+O"; onTriggered: root.openFirmware() }
            AppMenuItem { text: "Load Demo Firmware (builtin blinky)"; onTriggered: emulator.loadBuiltin() }
            MenuSeparator {}
            AppMenuItem { text: "Exit"; onTriggered: Qt.quit() }
        }

        AppMenu {
            title: "&Build"
            AppMenuItem {
                text: "Build & Load"
                shortcut: "Ctrl+B"
                enabled: !emulator.building
                onTriggered: root.buildAndLoad()
            }
            AppMenu {
                id: optMenu
                title: "Optimization: " + emulator.optimization
                Instantiator {
                    model: emulator.optimizationLevels
                    delegate: AppMenuItem {
                        readonly property var notes: ({
                            "-O0": "none, best for debugging (Debug)",
                            "-Og": "optimize, keep debugging usable",
                            "-O1": "basic",
                            "-O2": "speed (Release)",
                            "-O3": "maximum speed",
                            "-Os": "size (CubeIDE Release)"
                        })
                        text: modelData + "   " + notes[modelData]
                        checkable: true
                        checked: emulator.optimization === modelData
                        onTriggered: emulator.optimization = modelData
                    }
                    onObjectAdded: (index, object) => optMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => optMenu.removeItem(object)
                }
            }
            MenuSeparator {}
            AppMenuItem { text: "Clock Configuration…"; onTriggered: clockDialog.open() }
        }

        AppMenu {
            title: "&Debug"
            AppMenuItem { text: "Run / Continue"; shortcut: "F5"; enabled: !emulator.running; onTriggered: root.runOrContinue() }
            AppMenuItem { text: "Pause"; shortcut: "Shift+F5"; enabled: emulator.running; onTriggered: root.pause() }
            AppMenuItem { text: "Reset"; shortcut: "Ctrl+Shift+F5"; onTriggered: emulator.reset() }
            MenuSeparator {}
            AppMenuItem { text: "Step Over"; shortcut: "F10"; enabled: root.canStep; onTriggered: emulator.stepOver() }
            AppMenuItem { text: "Step Into"; shortcut: "F11"; enabled: root.canStep; onTriggered: emulator.stepInto() }
            AppMenuItem { text: "Step Out"; shortcut: "Shift+F11"; enabled: root.canStep && debugSession.active; onTriggered: emulator.stepOut() }
            AppMenuItem { text: "Step Instruction"; shortcut: "Ctrl+F10"; enabled: root.canStep; onTriggered: emulator.step() }
            MenuSeparator {}
            AppMenuItem { text: "Toggle Breakpoint"; shortcut: "F9"; onTriggered: editorPanel.toggleBreakpointAtCursor() }
            AppMenuItem { text: "Delete All Breakpoints"; shortcut: "Ctrl+Shift+F9"; onTriggered: debugSession.clearBreakpoints() }
        }

        AppMenu {
            title: "&View"
            AppMenuItem { text: "Go to Line…"; shortcut: "Ctrl+G"; onTriggered: editorPanel.openGoToLine() }
            AppMenuItem {
                text: root.editorMaximized ? "Restore Panels" : "Expand Editor"
                shortcut: "Ctrl+M"
                onTriggered: root.toggleEditor()
            }
        }
    }

    header: Toolbar {
        dialogOpen: root.dialogOpen
        onOpenClock: clockDialog.open()
    }

    // ---- panels ------------------------------------------------------------------------
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
            visible: !root.editorMaximized
            SplitView.preferredWidth: 320
            SplitView.minimumWidth: 260
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
                    id: editorPanel
                    SplitView.fillWidth: true
                    maximized: root.editorMaximized
                    onToggleMaximized: root.toggleEditor()
                }
                DisasmPanel {
                    visible: !root.editorMaximized
                    SplitView.preferredWidth: 340
                    SplitView.minimumWidth: 240
                }
            }

            SplitView {
                visible: !root.editorMaximized
                SplitView.preferredHeight: 220
                SplitView.minimumHeight: 150
                orientation: Qt.Horizontal
                handle: splitHandle
                UartPanel { SplitView.fillWidth: true }
                BuildPanel { SplitView.preferredWidth: 360 }
            }
        }

        SplitView {
            visible: !root.editorMaximized
            SplitView.preferredWidth: 380
            SplitView.minimumWidth: 280
            orientation: Qt.Vertical
            handle: splitHandle
            DebugPanel { SplitView.fillHeight: true; SplitView.minimumHeight: 200 }
            MemoryPanel { SplitView.preferredHeight: 250; SplitView.minimumHeight: 120 }
        }
    }

    // ---- shortcuts (same actions as the menu) --------------------------------------------
    Shortcut { sequence: "Ctrl+N"; context: Qt.ApplicationShortcut; onActivated: root.newProject() }
    Shortcut { sequence: "Ctrl+O"; context: Qt.ApplicationShortcut; onActivated: root.openProject() }
    Shortcut { sequence: "Ctrl+S"; context: Qt.ApplicationShortcut; onActivated: projectModel.saveAll() }
    Shortcut { sequence: "Ctrl+Shift+O"; context: Qt.ApplicationShortcut; onActivated: root.openFirmware() }
    Shortcut { sequence: "Ctrl+B"; context: Qt.ApplicationShortcut; onActivated: root.buildAndLoad() }
    Shortcut { sequence: "F5"; context: Qt.ApplicationShortcut; onActivated: root.runOrContinue() }
    Shortcut { sequence: "Shift+F5"; context: Qt.ApplicationShortcut; onActivated: root.pause() }
    Shortcut { sequence: "Ctrl+Shift+F5"; context: Qt.ApplicationShortcut; onActivated: emulator.reset() }
    Shortcut { sequence: "F9"; context: Qt.ApplicationShortcut; onActivated: editorPanel.toggleBreakpointAtCursor() }
    Shortcut { sequence: "Ctrl+Shift+F9"; context: Qt.ApplicationShortcut; onActivated: debugSession.clearBreakpoints() }
    Shortcut { sequence: "F10"; context: Qt.ApplicationShortcut; enabled: root.canStep; onActivated: emulator.stepOver() }
    Shortcut { sequence: "F11"; context: Qt.ApplicationShortcut; enabled: root.canStep; onActivated: emulator.stepInto() }
    Shortcut { sequence: "Shift+F11"; context: Qt.ApplicationShortcut; enabled: root.canStep; onActivated: emulator.stepOut() }
    Shortcut { sequence: "Ctrl+F10"; context: Qt.ApplicationShortcut; enabled: root.canStep; onActivated: emulator.step() }
    Shortcut { sequence: "Ctrl+M"; context: Qt.ApplicationShortcut; onActivated: root.toggleEditor() }
    Shortcut { sequence: "Ctrl+G"; context: Qt.ApplicationShortcut; onActivated: editorPanel.openGoToLine() }

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
            Rectangle { width: 1; height: 12; color: Theme.border; visible: debugSession.active }
            Label {
                visible: debugSession.active
                text: "gdb: " + debugSession.state + (debugSession.location.length ? "  ·  " + debugSession.location : "")
                color: Theme.muted
                font.pixelSize: 11
                elide: Text.ElideRight
                Layout.maximumWidth: 420
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
