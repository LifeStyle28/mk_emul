import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: root
    property alias text: editor.text
    property alias font: editor.font
    signal edited()

    function load(s) {
        if (editor.text === s)
            return
        editor.text = s
    }

    readonly property var te: editor.contentItem
    readonly property int lineCount: editor.text.length === 0 ? 1 : editor.text.split("\n").length
    readonly property real lineH: {
        if (te && te.lineCount > 0 && te.contentHeight > 0)
            return te.contentHeight / te.lineCount
        return Math.ceil(editor.font.pixelSize * 1.4)
    }

    Rectangle {
        id: gutter
        width: Math.max(44, String(root.lineCount).length * 10 + 18)
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        color: "#0a1016"
        clip: true

        Repeater {
            model: Math.max(1, Math.ceil(flick.height / Math.max(8, root.lineH)) + 3)
            Text {
                property int lineNo: Math.floor(flick.contentY / Math.max(8, root.lineH)) + index
                visible: lineNo >= 0 && lineNo < root.lineCount
                y: editor.topPadding + lineNo * root.lineH - flick.contentY
                width: gutter.width - 8
                x: 4
                height: root.lineH
                text: lineNo + 1
                color: Theme.muted
                font.family: Theme.monoFont
                font.pixelSize: 11
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
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
    }
}
