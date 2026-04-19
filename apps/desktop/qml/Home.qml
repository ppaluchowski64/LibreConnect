import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LibreConnect.desktop 1.0

Page {
    id: root

    required property var windowRef
    required property var connectionController
    property string activeDeviceName: "Connected Device"
    property string activeDeviceId: ""
    property string initialFeature: ""
    property string selectedFeature: ""
    readonly property color destructiveFill: Theme.destructiveFillColor
    readonly property color destructiveFillHover: Theme.destructiveFillHoverColor
    readonly property color destructiveText: Theme.dangerColor
    readonly property string windowTitleSuffix: {
        if (selectedFeature === "fileManager")
            return "File Manager"
        if (selectedFeature === "cameras")
            return "Virtual Camera"
        if (selectedFeature === "settings")
            return "Settings"
        return ""
    }

    function selectFeature(featureKey) {
        const nextFeature = featureKey ? featureKey : ""
        if (selectedFeature === nextFeature) {
            if (windowRef && windowRef.updateWindowTitle !== undefined)
                windowRef.updateWindowTitle()
            return
        }

        selectedFeature = nextFeature

        if (selectedFeature.length === 0) {
            if (featureStack.depth > 0)
                featureStack.clear()
        } else {
            const pageUrl = "qrc:/LibreConnect/desktop/" + (
                selectedFeature === "fileManager" ? "FileManagerPage.qml"
                : selectedFeature === "cameras" ? "VirtualCameraPage.qml"
                : "SettingsPage.qml"
            )
            if (featureStack.depth === 0)
                featureStack.push(pageUrl)
            else
                featureStack.replace(pageUrl)
        }

        if (windowRef && windowRef.updateWindowTitle !== undefined)
            windowRef.updateWindowTitle()
    }

    background: Rectangle {
        color: Theme.backgroundColor
    }

    Dialog {
        id: disconnectConfirmDialog
        title: ""
        modal: true
        closePolicy: Popup.CloseOnEscape
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        background: Rectangle {
            radius: 10
            color: Theme.panelColor
            border.color: Theme.panelBorderColor
            border.width: 1
        }

        contentItem: Column {
            spacing: 12
            width: 360

            Text {
                text: "Disconnect device?"
                font.family: Theme.fontFamily
                font.pixelSize: 26
                font.bold: true
                color: Theme.textColor
            }

            Text {
                text: "You will be returned to the pairing screen."
                color: Theme.textColor
                font.family: Theme.fontFamily
                font.pixelSize: 15
                wrapMode: Text.WordWrap
                width: parent.width
            }

            Row {
                spacing: 10

                ThemedButton {
                    text: "Cancel"
                    width: 160
                    height: 42
                    font.pixelSize: 14
                    onClicked: disconnectConfirmDialog.close()
                }

                Rectangle {
                    width: 160
                    height: 42
                    radius: 8
                    color: disconnectConfirmMouse.containsMouse ? root.destructiveFillHover : root.destructiveFill
                    border.color: Theme.panelBorderColor
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "Disconnect"
                        font.family: Theme.fontFamily
                        font.pixelSize: 14
                        color: root.destructiveText
                    }

                    MouseArea {
                        id: disconnectConfirmMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            disconnectConfirmDialog.close()
                            connectionController.disconnect()
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: unpairConfirmDialog
        title: ""
        modal: true
        closePolicy: Popup.CloseOnEscape
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        background: Rectangle {
            radius: 10
            color: Theme.panelColor
            border.color: Theme.panelBorderColor
            border.width: 1
        }

        contentItem: Column {
            spacing: 12
            width: 360

            Text {
                text: "Unpair this device?"
                font.family: Theme.fontFamily
                font.pixelSize: 26
                font.bold: true
                color: Theme.textColor
            }

            Text {
                text: root.activeDeviceId.length > 0
                      ? "This removes the current device from paired devices and disconnects."
                      : "No paired device ID found for this session."
                color: Theme.textColor
                font.family: Theme.fontFamily
                font.pixelSize: 15
                wrapMode: Text.WordWrap
                width: parent.width
            }

            Row {
                spacing: 10

                ThemedButton {
                    text: "Cancel"
                    width: 160
                    height: 42
                    font.pixelSize: 14
                    onClicked: unpairConfirmDialog.close()
                }

                Rectangle {
                    width: 160
                    height: 42
                    radius: 8
                    color: unpairConfirmMouse.containsMouse ? root.destructiveFillHover : root.destructiveFill
                    border.color: Theme.panelBorderColor
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "Unpair"
                        font.family: Theme.fontFamily
                        font.pixelSize: 14
                        color: root.destructiveText
                    }

                    MouseArea {
                        id: unpairConfirmMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            unpairConfirmDialog.close()
                            if (root.activeDeviceId.length === 0)
                                return

                            const removed = connectionController.removePairedDevice(root.activeDeviceId)
                            if (removed)
                                connectionController.disconnect()
                        }
                    }
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        Rectangle {
            Layout.preferredWidth: 320
            Layout.minimumWidth: 270
            Layout.maximumWidth: 360
            Layout.fillHeight: true
            radius: 14
            color: Theme.panelColor
            border.color: Theme.panelBorderColor
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 14

                Rectangle {
                    Layout.fillWidth: true
                    radius: 10
                    color: Theme.backgroundColor
                    border.color: Theme.panelBorderColor
                    border.width: 1
                    implicitHeight: 96

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 12

                        Image {
                            source: "libreconnect_logo.png"
                            Layout.preferredWidth: 62
                            Layout.preferredHeight: 62
                            sourceSize.width: 62
                            sourceSize.height: 62
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                        }

                        Column {
                            Layout.fillWidth: true
                            spacing: 3

                            Text {
                                text: root.activeDeviceName && root.activeDeviceName.length > 0
                                      ? root.activeDeviceName
                                      : "Connected Device"
                                font.family: Theme.fontFamily
                                font.pixelSize: 18
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                                color: Theme.textColor
                                width: parent.width
                            }

                            Text {
                                text: "Connected"
                                font.family: Theme.fontFamily
                                font.pixelSize: 16
                                color: Theme.mutedTextColor
                            }
                        }

                        Item {
                            Layout.alignment: Qt.AlignBottom
                            width: 82
                            height: 36

                            Row {
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                spacing: 8

                                Rectangle {
                                    id: moreButton
                                    width: 36
                                    height: 36
                                    radius: 10
                                    color: moreMouse.containsMouse ? Theme.buttonColor : Theme.backgroundColor
                                    border.color: Theme.panelBorderColor
                                    border.width: 1

                                    Image {
                                        anchors.centerIn: parent
                                        width: 22
                                        height: 22
                                        sourceSize.width: 22
                                        sourceSize.height: 22
                                        fillMode: Image.PreserveAspectFit
                                        source: Theme.dark ? "more_dark.svg" : "more.svg"
                                    }

                                    MouseArea {
                                        id: moreMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: actionsPopup.open()
                                    }
                                }

                                Rectangle {
                                    width: 36
                                    height: 36
                                    radius: 10
                                    color: settingsMouse.containsMouse ? Theme.buttonColor : Theme.backgroundColor
                                    border.color: Theme.panelBorderColor
                                    border.width: 1

                                    Image {
                                        anchors.centerIn: parent
                                        width: 22
                                        height: 22
                                        sourceSize.width: 22
                                        sourceSize.height: 22
                                        fillMode: Image.PreserveAspectFit
                                        source: Theme.dark ? "settings_dark.svg" : "settings.svg"
                                    }

                                    MouseArea {
                                        id: settingsMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: root.selectFeature("settings")
                                    }
                                }
                            }
                        }
                    }
                }

                Popup {
                    id: actionsPopup
                    parent: Overlay.overlay
                    x: 0
                    y: 0
                    modal: false
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                    padding: 8
                    onOpened: {
                        const pt = moreButton.mapToItem(Overlay.overlay, 0, moreButton.height + 8)
                        x = Math.max(12, Math.min(pt.x - width + moreButton.width, Overlay.overlay.width - width - 12))
                        y = Math.max(12, pt.y)
                    }
                    background: Rectangle {
                        radius: 10
                        color: Theme.panelColor
                        border.color: Theme.panelBorderColor
                        border.width: 1
                    }

                    Column {
                        spacing: 8

                        Rectangle {
                            width: 190
                            height: 38
                            radius: 8
                            color: disconnectMouse.containsMouse ? root.destructiveFillHover : root.destructiveFill
                            border.color: Theme.panelBorderColor
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: "Disconnect"
                                font.family: Theme.fontFamily
                                font.pixelSize: 14
                                color: root.destructiveText
                            }

                            MouseArea {
                                id: disconnectMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    actionsPopup.close()
                                    disconnectConfirmDialog.open()
                                }
                            }
                        }

                        Rectangle {
                            width: 190
                            height: 38
                            radius: 8
                            color: unpairMouse.containsMouse ? root.destructiveFillHover : root.destructiveFill
                            border.color: Theme.panelBorderColor
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: "Unpair"
                                font.family: Theme.fontFamily
                                font.pixelSize: 14
                                color: root.destructiveText
                            }

                            MouseArea {
                                id: unpairMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    actionsPopup.close()
                                    unpairConfirmDialog.open()
                                }
                            }
                        }
                    }
                }

                Column {
                    Layout.fillWidth: true
                    spacing: 12

                    ThemedButton {
                        text: "Cameras"
                        width: parent.width
                        height: 58
                        onClicked: windowRef.showVirtualCamera()
                    }

                    ThemedButton {
                        text: "File Manager"
                        width: parent.width
                        height: 58
                        onClicked: windowRef.showFileManager()
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            radius: 14
            color: Theme.panelColor
            border.color: Theme.panelBorderColor
            border.width: 1

            StackView {
                id: featureStack
                anchors.fill: parent
                anchors.margins: 14
                visible: depth > 0

                pushEnter: Transition {
                    PropertyAnimation {
                        property: "opacity"
                        from: 0
                        to: 1
                        duration: 300
                    }
                    PropertyAnimation {
                        property: "x"
                        from: featureStack.width
                        to: 0
                        duration: 300
                        easing.type: Easing.OutCubic
                    }
                }

                pushExit: Transition {
                    PropertyAnimation {
                        property: "opacity"
                        from: 1
                        to: 0
                        duration: 300
                    }
                    PropertyAnimation {
                        property: "x"
                        from: 0
                        to: -featureStack.width * 0.3
                        duration: 300
                        easing.type: Easing.OutCubic
                    }
                }

                replaceEnter: Transition {
                    PropertyAnimation {
                        property: "opacity"
                        from: 0
                        to: 1
                        duration: 300
                    }
                    PropertyAnimation {
                        property: "x"
                        from: featureStack.width
                        to: 0
                        duration: 300
                        easing.type: Easing.OutCubic
                    }
                }

                replaceExit: Transition {
                    PropertyAnimation {
                        property: "opacity"
                        from: 1
                        to: 0
                        duration: 300
                    }
                    PropertyAnimation {
                        property: "x"
                        from: 0
                        to: -featureStack.width * 0.3
                        duration: 300
                        easing.type: Easing.OutCubic
                    }
                }
            }

            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 420)
                spacing: 8
                visible: featureStack.depth === 0

                Text {
                    text: "No feature selected"
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    font.family: Theme.fontFamily
                    font.pixelSize: 26
                    font.bold: true
                    color: Theme.textColor
                }

                Text {
                    text: "Select a feature on the left"
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    font.family: Theme.fontFamily
                    font.pixelSize: 18
                    color: Theme.mutedTextColor
                }
            }
        }
    }

    Component.onCompleted: {
        if (windowRef && windowRef.applyHomeWindowSize !== undefined)
            windowRef.applyHomeWindowSize()
        selectFeature(initialFeature)
    }
}
