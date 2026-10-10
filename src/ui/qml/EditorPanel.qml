import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

PanelCard {
    id: root
    title: "Source"
    badge: (projectModel.dirty ? "● " : "") + (projectModel.currentName.length ? projectModel.currentName : "no file")
    property bool maximized: false
    signal toggleMaximized()

    // F9: breakpoint on the line of the text cursor.
    function toggleBreakpointAtCursor() {
        debugSession.toggleBreakpoint(projectModel.currentName, code.cursorLine())
    }

    function openGoToLine() {
        goToLine.open()
        lineInput.text = ""
        lineInput.forceActiveFocus()
    }

    // The debugger stopped: show its file and line.
    function showLocation() {
        if (!debugSession.currentFile.length || debugSession.currentLine <= 0)
            return
        const i = projectModel.indexOfName(debugSession.currentFile)
        if (i >= 0)
            projectModel.currentIndex = i
        Qt.callLater(function() { code.ensureVisible(debugSession.currentLine) })
    }

    function addFromField() {
        const n = newName.text.trim().length ? newName.text : "new.c"
        if (projectModel.addFile(n))
            newName.text = ""
    }

    function startRename(idx) {
        if (idx < 0 || idx >= fileList.count)
            return
        projectModel.currentIndex = idx
        fileList.renamingIndex = idx
        fileList.forceActiveFocus()
    }

    function askDelete(idx) {
        if (idx < 0 || idx >= fileList.count)
            return
        deleteDialog.targetIndex = idx
        deleteDialog.open()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            color: Theme.header
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 6
                TextField {
                    id: newName
                    Layout.preferredWidth: 140
                    implicitHeight: 28
                    placeholderText: "new.c"
                    font.family: Theme.monoFont
                    font.pixelSize: 12
                    color: Theme.text
                    leftPadding: 8
                    background: Rectangle {
                        radius: 6
                        color: Theme.panelAlt
                        border.color: parent.activeFocus ? Theme.accent : Theme.border
                    }
                    onAccepted: root.addFromField()
                }
                AppButton {
                    text: "Add file"
                    onClicked: root.addFromField()
                }
                Label {
                    Layout.fillWidth: true
                    text: projectModel.projectPath
                    color: Theme.muted
                    font.pixelSize: 10
                    font.family: Theme.monoFont
                    elide: Text.ElideMiddle
                }
                AppButton {
                    text: root.maximized ? "Restore" : "Expand"
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: root.maximized ? "Show all panels again (Ctrl+M)"
                                                 : "Editor over the whole window (Ctrl+M)"
                    onClicked: root.toggleMaximized()
                }
            }
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            handle: Rectangle {
                implicitWidth: 6
                color: SplitHandle.hovered ? Theme.accentDim : "transparent"
            }

            ListView {
                id: fileList
                property int renamingIndex: -1
                SplitView.preferredWidth: 180
                SplitView.minimumWidth: 120
                clip: true
                focus: true
                activeFocusOnTab: true
                keyNavigationEnabled: true
                model: projectModel
                currentIndex: projectModel.currentIndex
                highlight: Rectangle { color: Theme.accentDim }
                highlightMoveDuration: 0
                boundsBehavior: Flickable.StopAtBounds

                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_F2) {
                        root.startRename(projectModel.currentIndex)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
                        if (renamingIndex < 0)
                            root.askDelete(projectModel.currentIndex)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Escape && renamingIndex >= 0) {
                        renamingIndex = -1
                        event.accepted = true
                    }
                }

                delegate: Item {
                    id: rowRoot
                    width: ListView.view.width
                    height: 26

                    Rectangle {
                        anchors.fill: parent
                        color: hover.containsMouse ? "#18222e" : "transparent"
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 6
                        Label {
                            text: dirty ? "●" : " "
                            color: Theme.accent
                            font.pixelSize: 10
                            Layout.preferredWidth: 10
                            visible: fileList.renamingIndex !== index
                        }
                        Label {
                            text: name
                            visible: fileList.renamingIndex !== index
                            color: index === projectModel.currentIndex ? Theme.accent : Theme.text
                            font.family: Theme.monoFont
                            font.pixelSize: 12
                            elide: Text.ElideLeft
                            Layout.fillWidth: true
                        }
                        TextField {
                            id: renameField
                            property bool armed: false
                            visible: fileList.renamingIndex === index
                            Layout.fillWidth: true
                            implicitHeight: 22
                            font.family: Theme.monoFont
                            font.pixelSize: 12
                            color: Theme.text
                            leftPadding: 4
                            rightPadding: 4
                            background: Rectangle {
                                radius: 4
                                color: Theme.panelAlt
                                border.color: Theme.accent
                            }
                            onVisibleChanged: {
                                if (visible) {
                                    armed = false
                                    text = name
                                    Qt.callLater(function() {
                                        renameField.forceActiveFocus()
                                        renameField.selectAll()
                                        renameField.armed = true
                                    })
                                } else {
                                    armed = false
                                }
                            }
                            onAccepted: {
                                if (projectModel.renameFile(index, text))
                                    fileList.renamingIndex = -1
                            }
                            Keys.onEscapePressed: fileList.renamingIndex = -1
                            onActiveFocusChanged: {
                                if (!activeFocus && armed && fileList.renamingIndex === index)
                                    fileList.renamingIndex = -1
                            }
                        }
                    }

                    MouseArea {
                        id: hover
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        enabled: fileList.renamingIndex !== index
                        onClicked: {
                            projectModel.currentIndex = index
                            fileList.forceActiveFocus()
                            if (mouse.button === Qt.RightButton) {
                                fileMenu.targetIndex = index
                                fileMenu.popup()
                            }
                        }
                        onDoubleClicked: root.startRename(index)
                    }
                }

                ScrollBar.vertical: ScrollBar {}

                Label {
                    anchors.centerIn: parent
                    visible: fileList.count === 0
                    text: "No files"
                    color: Theme.muted
                    font.pixelSize: 12
                }
            }

            CodeEditor {
                id: code
                SplitView.fillWidth: true
                breakpoints: {
                    debugSession.breakpointsVersion // re-read when breakpoints change
                    return debugSession.breakpointLines(projectModel.currentName)
                }
                currentLine: debugSession.currentFile === projectModel.currentName ? debugSession.currentLine : -1
                onGutterClicked: function(line) { debugSession.toggleBreakpoint(projectModel.currentName, line) }
                onEdited: {
                    if (text !== projectModel.currentContent)
                        projectModel.currentContent = text
                }
            }
        }
    }

    Menu {
        id: fileMenu
        property int targetIndex: -1
        padding: 4
        palette.window: Theme.panel
        palette.windowText: Theme.text
        palette.base: Theme.panelAlt
        palette.text: Theme.text
        palette.button: Theme.panel
        palette.buttonText: Theme.text
        palette.highlight: Theme.accentDim
        palette.highlightedText: Theme.accent

        background: Rectangle {
            implicitWidth: 220
            color: Theme.panel
            border.color: Theme.border
            radius: 6
        }

        MenuItem {
            text: "Rename\tF2"
            enabled: fileMenu.targetIndex >= 0
            onTriggered: root.startRename(fileMenu.targetIndex)
        }
        MenuItem {
            text: "Duplicate"
            enabled: fileMenu.targetIndex >= 0
            onTriggered: projectModel.duplicateFile(fileMenu.targetIndex)
        }
        MenuSeparator {}
        MenuItem {
            text: "Delete\tDel"
            enabled: fileMenu.targetIndex >= 0
            onTriggered: root.askDelete(fileMenu.targetIndex)
        }
        MenuSeparator {}
        MenuItem {
            text: "Show in Explorer"
            enabled: fileMenu.targetIndex >= 0
            onTriggered: projectModel.revealInExplorer(fileMenu.targetIndex)
        }
    }

    Popup {
        id: goToLine
        parent: code
        x: 60
        y: 8
        width: 200
        padding: 8
        modal: false
        focus: true
        background: Rectangle { color: Theme.panel; border.color: Theme.accent; radius: 6 }
        RowLayout {
            anchors.fill: parent
            spacing: 6
            Label { text: "Go to line"; color: Theme.muted; font.pixelSize: 11 }
            TextField {
                id: lineInput
                Layout.fillWidth: true
                implicitHeight: 26
                font.family: Theme.monoFont
                font.pixelSize: 12
                color: Theme.text
                validator: IntValidator { bottom: 1 }
                background: Rectangle { color: Theme.panelAlt; radius: 4; border.color: Theme.border }
                onAccepted: {
                    goToLine.close()
                    code.goToLine(parseInt(text))
                }
                Keys.onEscapePressed: goToLine.close()
            }
        }
    }

    Dialog {
        id: deleteDialog
        property int targetIndex: -1
        title: "Delete file"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 380
        padding: 16
        standardButtons: Dialog.Cancel | Dialog.Yes

        background: Rectangle {
            color: Theme.panel
            border.color: Theme.border
            radius: 8
        }

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            color: Theme.text
            font.pixelSize: 13
            text: deleteDialog.targetIndex >= 0
                  ? "Delete \"" + projectModel.nameAt(deleteDialog.targetIndex) + "\" from disk?"
                  : ""
        }

        onAccepted: projectModel.removeFile(targetIndex)
    }

    Shortcut {
        sequence: "F2"
        enabled: fileList.activeFocus || fileList.renamingIndex >= 0
        onActivated: root.startRename(projectModel.currentIndex)
    }

    Connections {
        target: debugSession
        function onLocationChanged() { root.showLocation() }
    }

    Connections {
        target: projectModel
        function onCurrentIndexChanged() { code.load(projectModel.currentContent) }
        function onCurrentContentChanged() { code.load(projectModel.currentContent) }
        function onProjectPathChanged() { code.load(projectModel.currentContent) }
    }

    Component.onCompleted: code.load(projectModel.currentContent)
}
