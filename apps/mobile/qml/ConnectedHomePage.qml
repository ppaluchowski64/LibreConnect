import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Page {
    id: page

    required property var conn
    required property var clipboardSyncController
    required property var remoteInputController
    required property var showRemoteInputCallback
    required property var showPresenterModeCallback
    required property var showRemoteKeyboardCallback
    required property var showSettingsCallback
    property int logoTapCount: 0

    readonly property int menuWidth: 190

    function themedIcon(baseName) {
        return Theme.dark
                ? ("qrc:/LibreConnect/mobile/" + baseName + "_dark.svg")
                : ("qrc:/LibreConnect/mobile/" + baseName + ".svg")
    }

    function handleLogoTap() {
        logoTapCount += 1
        if (logoTapCount >= 5) {
            logoTapCount = 0
            logoTapResetTimer.stop()
            page.conn.exportLogs()
            return
        }

        logoTapResetTimer.restart()
    }

    background: Rectangle {
        color: Theme.backgroundColor
    }

    Timer {
        id: logoTapResetTimer
        interval: 1500
        repeat: false
        onTriggered: page.logoTapCount = 0
    }

    component FeatureTile: Button {
        id: featureTile
        required property string labelText
        required property string iconBase

        Layout.fillWidth: true
        Layout.preferredHeight: 156
        hoverEnabled: true
        padding: 12

        background: Rectangle {
            radius: 14
            color: featureTile.pressed
                   ? (Theme.dark ? Qt.lighter(Theme.buttonColor, 1.18) : Qt.darker(Theme.buttonColor, 1.06))
                   : (featureTile.hovered
                      ? (Theme.dark ? Qt.lighter(Theme.buttonColor, 1.08) : Qt.darker(Theme.buttonColor, 1.03))
                      : Theme.buttonColor)
            border.width: 1
            border.color: Theme.panelBorderColor
        }

        contentItem: Item {
            anchors.fill: parent

            Column {
                anchors.centerIn: parent
                width: parent.width - (featureTile.padding * 2)
                spacing: 10

                Image {
                    anchors.horizontalCenter: parent.horizontalCenter
                    source: page.themedIcon(featureTile.iconBase)
                    sourceSize.width: 40
                    sourceSize.height: 40
                    width: 40
                    height: 40
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }

                Text {
                    width: parent.width
                    text: featureTile.labelText
                    color: Theme.textColor
                    font.pixelSize: 18
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                }
            }
        }
    }

    component BottomNavButton: Button {
        id: navButton
        required property string labelText
        required property string iconBase
        property bool selected: false

        Layout.fillWidth: true
        Layout.preferredWidth: 1
        Layout.preferredHeight: 60
        checkable: false
        hoverEnabled: true
        padding: 8

        background: Rectangle {
            radius: 14
            color: navButton.selected
                   ? Theme.selectedColor
                   : (navButton.pressed
                      ? (Theme.dark ? Qt.lighter(Theme.buttonColor, 1.14) : Qt.darker(Theme.buttonColor, 1.05))
                      : Theme.buttonColor)
            border.width: navButton.selected ? 2 : 1
            border.color: navButton.selected ? Theme.accentColor : Theme.panelBorderColor
        }

        contentItem: Item {
            anchors.fill: parent

            Column {
                anchors.centerIn: parent
                width: parent.width - (navButton.padding * 2)
                spacing: 2

                Image {
                    anchors.horizontalCenter: parent.horizontalCenter
                    source: page.themedIcon(navButton.iconBase)
                    sourceSize.width: 24
                    sourceSize.height: 24
                    width: 24
                    height: 24
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }

                Text {
                    width: parent.width
                    text: navButton.labelText
                    color: navButton.selected ? Theme.selectedTextColor : Theme.textColor
                    font.pixelSize: 13
                    font.bold: navButton.selected
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                }
            }
        }
    }

    Menu {
        id: connectedMenu
        parent: Overlay.overlay
        width: page.menuWidth
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            radius: 14
            color: Theme.panelColor
            border.width: 1
            border.color: Theme.panelBorderColor
        }

        enter: Transition {
            ParallelAnimation {
                NumberAnimation {
                    property: "opacity"
                    from: 0
                    to: 1
                    duration: 170
                    easing.type: Easing.OutCubic
                }
                NumberAnimation {
                    property: "scale"
                    from: 0.95
                    to: 1.0
                    duration: 170
                    easing.type: Easing.OutCubic
                }
            }
        }

        exit: Transition {
            ParallelAnimation {
                NumberAnimation {
                    property: "opacity"
                    from: 1
                    to: 0
                    duration: 120
                    easing.type: Easing.OutCubic
                }
                NumberAnimation {
                    property: "scale"
                    from: 1.0
                    to: 0.97
                    duration: 120
                    easing.type: Easing.OutCubic
                }
            }
        }

        MenuItem {
            text: "Disconnect"
            contentItem: Text {
                text: parent.text
                color: Theme.destructiveColor
                verticalAlignment: Text.AlignVCenter
            }
            onTriggered: disconnectDialog.open()
        }

        MenuItem {
            text: "Unpair"
            contentItem: Text {
                text: parent.text
                color: Theme.destructiveColor
                verticalAlignment: Text.AlignVCenter
            }
            onTriggered: unpairDialog.open()
        }
    }

    Dialog {
        id: disconnectDialog
        title: ""
        modal: true
        anchors.centerIn: Overlay.overlay
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        contentWidth: Math.min(page.width - 28, 340)
        Overlay.modal: Rectangle {
            color: Theme.dark ? "#99000000" : "#73000000"
        }
        background: Rectangle {
            radius: 16
            color: Theme.panelColor
            border.width: 1
            border.color: Theme.panelBorderColor
        }

        contentItem: Column {
            spacing: 14
            width: disconnectDialog.contentWidth

            Text {
                width: parent.width
                text: "Disconnect device?"
                color: Theme.textColor
                font.pixelSize: 22
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                text: "You will be returned to the connection screen."
                color: Theme.mutedTextColor
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }

            RowLayout {
                width: parent.width
                spacing: 10

                Button {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.minimumWidth: 0
                    text: "Cancel"
                    onClicked: disconnectDialog.close()
                }

                Button {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.minimumWidth: 0
                    text: "Disconnect"
                    contentItem: Text {
                        text: parent.text
                        color: Theme.destructiveColor
                        anchors.fill: parent
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        disconnectDialog.close()
                        page.conn.disconnect()
                    }
                }
            }
        }
    }

    Dialog {
        id: unpairDialog
        title: ""
        modal: true
        anchors.centerIn: Overlay.overlay
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        contentWidth: Math.min(page.width - 28, 340)
        Overlay.modal: Rectangle {
            color: Theme.dark ? "#99000000" : "#73000000"
        }
        background: Rectangle {
            radius: 16
            color: Theme.panelColor
            border.width: 1
            border.color: Theme.panelBorderColor
        }

        contentItem: Column {
            spacing: 14
            width: unpairDialog.contentWidth

            Text {
                width: parent.width
                text: "Unpair this device?"
                color: Theme.textColor
                font.pixelSize: 22
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                text: "This removes the current desktop from paired devices and disconnects."
                color: Theme.mutedTextColor
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }

            RowLayout {
                width: parent.width
                spacing: 10

                Button {
                    Layout.fillWidth: true
                    text: "Cancel"
                    onClicked: unpairDialog.close()
                }

                Button {
                    Layout.fillWidth: true
                    text: "Unpair"
                    contentItem: Text {
                        text: parent.text
                        color: Theme.destructiveColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        unpairDialog.close()
                        page.conn.unpairCurrentDevice()
                    }
                }
            }
        }
    }

    function openConnectedMenu(anchorItem) {
        if (!Overlay.overlay) {
            connectedMenu.open()
            return
        }

        Qt.callLater(function() {
            const overlay = Overlay.overlay
            if (!overlay) {
                connectedMenu.open()
                return
            }

            const anchorWidth = anchorItem.width > 0 ? anchorItem.width : anchorItem.implicitWidth
            const anchorHeight = anchorItem.height > 0 ? anchorItem.height : anchorItem.implicitHeight
            const point = anchorItem.mapToItem(
                overlay,
                anchorWidth - connectedMenu.width,
                anchorHeight + 8
            )
            connectedMenu.x = Math.max(12, Math.min(point.x, overlay.width - connectedMenu.width - 12))
            connectedMenu.y = Math.max(12, point.y)
            connectedMenu.open()
        })
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                anchors.topMargin: 12
                anchors.bottomMargin: 8
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true

                    Item {
                        Layout.preferredWidth: moreButton.implicitWidth
                        Layout.preferredHeight: moreButton.implicitHeight
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    ToolButton {
                        id: moreButton
                        icon.source: Theme.dark
                                     ? "qrc:/LibreConnect/mobile/more_dark.svg"
                                     : "qrc:/LibreConnect/mobile/more.svg"
                        icon.width: 24
                        icon.height: 24
                        display: AbstractButton.IconOnly
                        onClicked: page.openConnectedMenu(moreButton)
                    }
                }

                Item {
                    Layout.fillHeight: true
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    RoundedLogo {
                        Layout.alignment: Qt.AlignHCenter
                        width: 96
                        height: 96

                        MouseArea {
                            anchors.fill: parent
                            onClicked: page.handleLogoTap()
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "Connected"
                        color: Theme.textColor
                        font.pixelSize: 52
                        font.bold: false
                        horizontalAlignment: Text.AlignHCenter
                        minimumPixelSize: 24
                        fontSizeMode: Text.Fit
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    rowSpacing: 10
                    columnSpacing: 10

                    FeatureTile {
                        labelText: "Media Remote"
                        iconBase: "media"
                        onClicked: page.showRemoteInputCallback()
                    }

                    FeatureTile {
                        labelText: "Remote Keyboard"
                        iconBase: "keyboard"
                        onClicked: page.showRemoteKeyboardCallback()
                    }

                    FeatureTile {
                        labelText: "Presenter Mode"
                        iconBase: "presenter"
                        onClicked: page.showPresenterModeCallback()
                    }

                    FeatureTile {
                        labelText: page.clipboardSyncController.busy ? "Syncing Clipboard..." : "Sync Clipboard"
                        iconBase: "clipboard"
                        enabled: !page.clipboardSyncController.busy
                        onClicked: page.clipboardSyncController.syncClipboard()
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
            }
        }

        Frame {
            Layout.fillWidth: true
            padding: 10
            Material.elevation: 4

            background: Rectangle {
                radius: 16
                color: Theme.panelColor
                border.width: 1
                border.color: Theme.panelBorderColor
            }

            RowLayout {
                anchors.fill: parent
                spacing: 10

                BottomNavButton {
                    labelText: "Home"
                    iconBase: "home"
                    selected: true
                }

                BottomNavButton {
                    labelText: "Settings"
                    iconBase: "settings"
                    selected: false
                    onClicked: page.showSettingsCallback()
                }
            }
        }
    }
}
