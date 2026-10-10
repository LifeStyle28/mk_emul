import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: root
    property alias text: editor.text
    property alias font: editor.font
    // Debugger: breakpoints of this file ([{line, state}]: ok / pending / error) and the line
    // the program stopped at (-1: none). A click on a line number toggles a breakpoint.
    property var breakpoints: []
    property int currentLine: -1
    signal edited()
    signal gutterClicked(int line)

    function load(s) {
        if (editor.text === s)
            return
        editor.text = s
    }

    // 1-based line of the text cursor (F9 toggles a breakpoint there).
    function cursorLine() {
        return editor.text.substring(0, editor.cursorPosition).split("\n").length
    }

    // Puts the text cursor at the start of a line and focuses the editor (Ctrl+G).
    function goToLine(line) {
        const lines = editor.text.split("\n")
        line = Math.max(1, Math.min(line, lines.length))
        let pos = 0
        for (let i = 0; i < line - 1; ++i)
            pos += lines[i].length + 1
        editor.cursorPosition = pos
        editor.forceActiveFocus()
        ensureVisible(line)
    }

    // Scrolls so that the line is visible (a third from the top when it was not).
    function ensureVisible(line) {
        if (line <= 0)
            return
        const y = editor.topPadding + (line - 1) * root.lineH
        if (y < flick.contentY || y + root.lineH > flick.contentY + flick.height) {
            const maxY = Math.max(0, flick.contentHeight - flick.height)
            flick.contentY = Math.min(maxY, Math.max(0, y - flick.height / 3))
        }
    }

    function breakpointState(line) {
        for (let i = 0; i < root.breakpoints.length; ++i) {
            if (root.breakpoints[i].line === line)
                return root.breakpoints[i].state
        }
        return ""
    }

    readonly property int lineCount: editor.text.length === 0 ? 1 : editor.text.split("\n").length
    // TextArea is itself the text item (it has no contentItem): measure its real line height,
    // otherwise the gutter numbers drift away from the text lines.
    readonly property real lineH: {
        if (editor.lineCount > 0 && editor.contentHeight > 0)
            return editor.contentHeight / editor.lineCount
        return Math.ceil(editor.font.pixelSize * 1.4)
    }

    Rectangle {
        id: gutter
        width: Math.max(44, String(root.lineCount).length * 10 + 18) + 16
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        color: "#0a1016"
        clip: true

        Repeater {
            model: Math.max(1, Math.ceil(flick.height / Math.max(8, root.lineH)) + 3)
            Item {
                property int lineNo: Math.floor(flick.contentY / Math.max(8, root.lineH)) + index
                readonly property string bp: root.breakpointState(lineNo + 1)
                visible: lineNo >= 0 && lineNo < root.lineCount
                y: editor.topPadding + lineNo * root.lineH - flick.contentY
                width: gutter.width
                height: root.lineH

                // Breakpoint: red dot; pending (no debug session yet): ring; gdb found no code: grey.
                Rectangle {
                    visible: parent.bp.length > 0
                    width: 10
                    height: 10
                    radius: 5
                    x: 4
                    anchors.verticalCenter: parent.verticalCenter
                    color: parent.bp === "ok" ? Theme.danger : "transparent"
                    border.color: parent.bp === "error" ? Theme.muted : Theme.danger
                    border.width: 2
                }
                // Current line of the stopped program.
                Text {
                    visible: parent.lineNo + 1 === root.currentLine
                    x: 15
                    anchors.verticalCenter: parent.verticalCenter
                    text: "➜"
                    color: Theme.warn
                    font.pixelSize: 12
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    height: parent.height
                    text: parent.lineNo + 1
                    color: parent.lineNo + 1 === root.currentLine ? Theme.warn : Theme.muted
                    font.family: Theme.monoFont
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: function(mouse) {
                const line = Math.floor((mouse.y + flick.contentY - editor.topPadding) / Math.max(8, root.lineH)) + 1
                if (line >= 1 && line <= root.lineCount)
                    root.gutterClicked(line)
            }
        }

        Rectangle {
            anchors.right: parent.right
            width: 1
            height: parent.height
            color: Theme.border
        }
    }

    Flickable {
        id: flick
        anchors.left: gutter.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.AutoFlickIfNeeded
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
        TextArea.flickable: editor
        TextArea {
            id: editor
            wrapMode: TextEdit.NoWrap
            selectByMouse: true
            persistentSelection: true
            font.family: Theme.monoFont
            font.pixelSize: 13
            color: Theme.text
            leftPadding: 12
            topPadding: 8
            bottomPadding: 8
            rightPadding: 8
            selectedTextColor: Theme.bg
            selectionColor: Theme.accent
            background: Rectangle { color: "#0d131a" }
            onTextChanged: root.edited()
            Component.onCompleted: emulator.attachHighlighter(textDocument)
        }
        // Highlight of the current line: a translucent band over the text (a sibling of the
        // TextArea in the Flickable's content, so it scrolls with the text).
        Rectangle {
            visible: root.currentLine > 0
            x: 0
            y: editor.topPadding + (root.currentLine - 1) * root.lineH
            width: Math.max(editor.width, flick.width)
            height: root.lineH
            color: Qt.rgba(0.94, 0.75, 0.35, 0.16)
            Rectangle {
                width: 3
                height: parent.height
                color: Theme.warn
            }
        }
    }
}
