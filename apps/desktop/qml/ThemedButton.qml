import QtQuick
import QtQuick.Controls
import LibreConnect.desktop 1.0

Button {
    id: control

    property string iconSource: ""
    property string darkIconSource: ""
    property int iconSize: 20
    readonly property string resolvedIconSource: {
        if (Theme.dark && darkIconSource.length > 0)
            return darkIconSource
        return iconSource
    }
    readonly property bool hasIcon: resolvedIconSource.length > 0
    readonly property bool hasText: text.length > 0

    font.family: Theme.fontFamily
    hoverEnabled: true

    contentItem: Item {
        implicitWidth: (control.hasIcon ? control.iconSize : 0)
                       + (control.hasIcon && control.hasText ? 8 : 0)
                       + (control.hasText ? labelText.implicitWidth : 0)
        implicitHeight: Math.max(control.hasIcon ? control.iconSize : 0,
                                 control.hasText ? labelText.implicitHeight : 0)

        Row {
            id: contentRow
            anchors.centerIn: parent
            spacing: control.hasIcon && control.hasText ? 8 : 0

            Image {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.hasIcon
                width: visible ? control.iconSize : 0
                height: visible ? control.iconSize : 0
                sourceSize.width: control.iconSize
                sourceSize.height: control.iconSize
                fillMode: Image.PreserveAspectFit
                smooth: true
                source: control.resolvedIconSource
            }

            Text {
                id: labelText
                anchors.verticalCenter: parent.verticalCenter
                visible: control.hasText
                width: visible ? Math.min(
                           implicitWidth,
                           Math.max(0, control.width - (control.hasIcon ? control.iconSize + contentRow.spacing : 0) - 16)
                       ) : 0
                text: control.text
                font: control.font
                color: Theme.textColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }
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
