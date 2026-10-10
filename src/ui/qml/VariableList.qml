import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

// Variables / watch expressions of the debugger as a tree: ▸ expands structs and arrays,
// a double click on a value edits it, values changed since the last stop are red.
ListView {
    id: list
    property bool watch: false
    signal removeRequested(int row)
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}

    delegate: Rectangle {
        id: row
        width: ListView.view.width
        height: 22
        color: model.header ? Theme.header : (index % 2 ? "transparent" : "#0c1218")

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 4 + model.depth * 14
            anchors.rightMargin: 4
            spacing: 4

            Label {
                text: model.header ? "" : (model.expandable ? (model.expanded ? "▾" : "▸") : "")
                Layout.preferredWidth: 10
                color: Theme.muted
                font.pixelSize: 11
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -4
                    enabled: model.expandable
                    onClicked: debugSession.toggleExpand(list.watch, index)
                }
            }
            Label {
                text: model.name
                Layout.preferredWidth: model.header ? -1 : Math.max(70, list.width * 0.34 - model.depth * 14)
                Layout.fillWidth: model.header
                elide: Text.ElideRight
                color: model.header ? Theme.accent : Theme.codeBlue
                font.family: model.header ? Theme.uiFont : Theme.monoFont
                font.pixelSize: model.header ? 11 : 12
                font.weight: model.header ? Font.DemiBold : Font.Normal
                MouseArea {
                    anchors.fill: parent
                    enabled: model.expandable
                    onClicked: debugSession.toggleExpand(list.watch, index)
                }
            }
            Label {
                id: valueLabel
                visible: !model.header && !editor.visible
                text: model.value
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: model.error ? Theme.muted : (model.changed ? Theme.danger : Theme.codeYellow)
                font.family: Theme.monoFont
                font.pixelSize: 12
                font.italic: model.error
                ToolTip.visible: valueMouse.containsMouse && model.value.length > 0
                ToolTip.delay: 600
                ToolTip.text: model.value + (model.type.length ? "\n" + model.type : "")
                MouseArea {
                    id: valueMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onDoubleClicked: {
                        if (model.error || model.expandable || debugSession.state !== "stopped")
                            return
                        editor.text = model.value.replace(/ '.*'$/, "") // 65 'A' -> 65
                        editor.visible = true
                        editor.forceActiveFocus()
                        editor.selectAll()
                    }
                }
            }
            TextField {
                id: editor
                visible: false
                Layout.fillWidth: true
                implicitHeight: 20
                font.family: Theme.monoFont
                font.pixelSize: 12
                color: Theme.text
                leftPadding: 4
                topPadding: 0
                bottomPadding: 0
                background: Rectangle { color: Theme.panelAlt; border.color: Theme.accent; radius: 3 }
                onAccepted: {
                    debugSession.setValue(list.watch, index, text)
                    visible = false
                }
                onActiveFocusChanged: if (!activeFocus) visible = false
                Keys.onEscapePressed: visible = false
            }
            Label {
                visible: !model.header && list.width > 330
                text: model.type
                Layout.preferredWidth: list.width * 0.22
                elide: Text.ElideRight
                color: Theme.muted
                font.family: Theme.monoFont
                font.pixelSize: 10
            }
            Label {
                visible: list.watch && model.depth === 0
                text: "×"
                color: removeMouse.containsMouse ? Theme.danger : Theme.muted
                font.pixelSize: 14
                MouseArea {
                    id: removeMouse
                    anchors.fill: parent
                    anchors.margins: -3
                    hoverEnabled: true
                    onClicked: list.removeRequested(index)
                }
            }
        }
    }
}
