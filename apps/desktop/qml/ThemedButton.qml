import QtQuick
import QtQuick.Controls
import LibreConnect.desktop 1.0

Button {
    id: control

    font.family: Theme.fontFamily
    hoverEnabled: true

    contentItem: Text {
        text: control.text
        font: control.font
        color: Theme.textColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: 8
        color: {
            if (!control.enabled)
                return Qt.darker(Theme.buttonColor, 1.15)
            if (control.down)
                return Qt.darker(Theme.buttonColor, 1.12)
            if (control.hovered)
                return Qt.lighter(Theme.buttonColor, 1.08)
            return Theme.buttonColor
        }
        border.color: Theme.panelBorderColor
        border.width: 1
    }
}
