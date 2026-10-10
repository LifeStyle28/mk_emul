import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

// Debugger views (gdb session): variables, watch expressions, call stack, breakpoints.
PanelCard {
    id: card
    title: "Debug"
    tabs: ["Variables", "Watch", "Call Stack", "Breakpoints"]
    badge: debugSession.active ? "gdb · " + debugSession.state : "no session"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Where the program stopped / what the debugger said last.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: statusText.implicitHeight + 10
            color: "#0c1218"
            Label {
                id: statusText
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                wrapMode: Text.Wrap
                font.pixelSize: 11
                color: debugSession.active ? Theme.text : Theme.muted
                text: {
                    if (!debugSession.active)
                        return debugSession.message.length ? debugSession.message
                             : "Build & Load starts the debugger (gdb). Click a line number in the editor to set a breakpoint, then Run (F5)."
                    if (debugSession.state === "running")
                        return "Running…  Pause (Shift+F5) or a breakpoint stops it."
                    var s = debugSession.location
                    if (debugSession.message.length && debugSession.message.indexOf(debugSession.location) < 0)
                        s = debugSession.message + (s.length ? "\n" + s : "")
                    return s
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: card.currentTab

            // Variables
            ColumnLayout {
                spacing: 0
                VariableList {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: debugSession.variables
                    opacity: debugSession.state === "running" ? 0.5 : 1
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 4
                    CheckBox {
                        text: "Hex"
                        checked: debugSession.hex
                        onToggled: debugSession.hex = checked
                        font.pixelSize: 11
                    }
                    Label {
                        Layout.fillWidth: true
                        text: "red = changed since the last stop · double-click a value to edit"
                        color: Theme.muted
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                }
            }

            // Watch
            ColumnLayout {
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 6
                    spacing: 6
                    TextField {
                        id: watchInput
                        Accessible.name: "Watch expression"
                        Layout.fillWidth: true
                        implicitHeight: 28
                        placeholderText: "expression: counter, values[2], p.x * 2, GPIOD->ODR"
                        font.family: Theme.monoFont
                        font.pixelSize: 12
                        color: Theme.text
                        leftPadding: 8
                        background: Rectangle {
                            radius: 6
                            color: Theme.panelAlt
                            border.color: parent.activeFocus ? Theme.accent : Theme.border
                        }
                        onAccepted: {
                            debugSession.addWatch(text)
                            text = ""
                        }
                    }
                    AppButton {
                        text: "Add"
                        onClicked: {
                            debugSession.addWatch(watchInput.text)
                            watchInput.text = ""
                        }
                    }
                }
                VariableList {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    watch: true
                    model: debugSession.watches
                    opacity: debugSession.state === "running" ? 0.5 : 1
                    onRemoveRequested: function(row) { debugSession.removeWatch(row) }
                }
            }

            // Call stack
            ListView {
                id: frameList
                clip: true
                model: debugSession.frames
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    width: ListView.view.width
                    height: 24
                    color: model.level === debugSession.selectedFrame ? Theme.accentDim
                           : (frameMouse.containsMouse ? "#18222e" : "transparent")
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8
                        Label {
                            text: "#" + model.level
                            color: Theme.muted
                            font.family: Theme.monoFont
                            font.pixelSize: 11
                        }
                        Label {
                            text: model.func + "()"
                            color: model.inProject ? Theme.codeBlue : Theme.muted
                            font.family: Theme.monoFont
                            font.pixelSize: 12
                        }
                        Label {
                            Layout.fillWidth: true
                            text: model.where
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideLeft
                            color: model.inProject ? Theme.text : Theme.muted
                            font.family: Theme.monoFont
                            font.pixelSize: 11
                        }
                    }
                    MouseArea {
                        id: frameMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: debugSession.selectFrame(model.level)
                    }
                }
                Label {
                    anchors.centerIn: parent
                    visible: frameList.count === 0
                    text: debugSession.state === "running" ? "running…" : "no call stack"
                    color: Theme.muted
                    font.pixelSize: 12
                }
            }

            // Breakpoints
            ColumnLayout {
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 6
                    spacing: 6
                    TextField {
                        id: bpAddress
                        Accessible.name: "Breakpoint address"
                        Layout.fillWidth: true
                        implicitHeight: 28
                        placeholderText: "address, e.g. 0x08000200"
                        font.family: Theme.monoFont
                        font.pixelSize: 12
                        color: Theme.text
                        leftPadding: 8
                        background: Rectangle {
                            radius: 6
                            color: Theme.panelAlt
                            border.color: parent.activeFocus ? Theme.accent : Theme.border
                        }
                        onAccepted: addAddress()
                        function addAddress() {
                            if (debugSession.addAddressBreakpoint(text))
                                text = ""
                        }
                    }
                    AppButton { text: "Add"; onClicked: bpAddress.addAddress() }
                    AppButton {
                        text: "Delete all"
                        danger: true
                        enabled: debugSession.breakpoints.count > 0
                        onClicked: debugSession.clearBreakpoints()
                    }
                }
                ListView {
                    id: bpList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: debugSession.breakpoints
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        width: ListView.view.width
                        height: model.error.length ? 38 : 24
                        color: index % 2 ? "transparent" : "#0c1218"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 8
                            Rectangle {
                                width: 10
                                height: 10
                                radius: 5
                                color: model.state === "ok" ? Theme.danger : "transparent"
                                border.color: model.state === "error" ? Theme.muted : Theme.danger
                                border.width: 2
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Label {
                                    Layout.fillWidth: true
                                    text: model.label
                                    color: model.state === "error" ? Theme.muted : Theme.text
                                    font.family: Theme.monoFont
                                    font.pixelSize: 12
                                    MouseArea {
                                        anchors.fill: parent
                                        enabled: model.file.length > 0
                                        onClicked: {
                                            const i = projectModel.indexOfName(model.file)
                                            if (i >= 0)
                                                projectModel.currentIndex = i
                                        }
                                    }
                                }
                                Label {
                                    visible: model.error.length > 0
                                    Layout.fillWidth: true
                                    text: model.error
                                    elide: Text.ElideRight
                                    color: Theme.warn
                                    font.pixelSize: 10
                                }
                            }
                            Label {
                                text: "×"
                                color: bpRemove.containsMouse ? Theme.danger : Theme.muted
                                font.pixelSize: 15
                                MouseArea {
                                    id: bpRemove
                                    anchors.fill: parent
                                    anchors.margins: -3
                                    hoverEnabled: true
                                    onClicked: debugSession.removeBreakpoint(index)
                                }
                            }
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        width: parent.width - 24
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        visible: bpList.count === 0
                        text: "No breakpoints. Click a line number in the editor or press F9."
                        color: Theme.muted
                        font.pixelSize: 12
                    }
                }
            }
        }
    }
}
