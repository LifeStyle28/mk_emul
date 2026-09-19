import QtQuick 2.15
import QtQuick.Controls 2.15

Button {
    id: control
    property bool primary: false
    property bool danger: false
    implicitHeight: 30
    leftPadding: 12
    rightPadding: 12
    font.family: Theme.uiFont
    font.pixelSize: 12
    font.weight: Font.DemiBold

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.down ? "#ffffff" : (control.primary ? "#06221f" : Theme.text)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        opacity: control.enabled ? 1 : 0.45
    }

    background: Rectangle {
        radius: 6
        color: {
            if (control.primary)
                return control.down ? "#2bb8a9" : (control.hovered ? "#4ee0d0" : Theme.accent)
            if (control.danger)
                return control.down ? "#c44b58" : (control.hovered ? "#ff8490" : "#6b3038")
            return control.down ? "#243040" : (control.hovered ? "#243041" : Theme.panelAlt)
        }
        border.color: control.primary ? "#5eecdc" : Theme.border
        border.width: 1
    }
}
