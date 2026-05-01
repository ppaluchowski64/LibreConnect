import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LibreConnect.desktop 1.0

Page {
    id: root

    required property var windowRef
    required property var connectionController
    required property var notificationSyncController
    required property var clipboardSyncController
    required property var smsBridgeController
    required property var permissionStateController
    required property var temporaryStorageController
    property string activeDeviceName: "Connected Device"
    property string activeDeviceId: ""
    property string initialFeature: ""
    property string selectedFeature: ""
    readonly property color destructiveFill: Theme.destructiveFillColor
    readonly property color destructiveFillHover: Theme.destructiveFillHoverColor
    readonly property color destructiveText: Theme.dangerColor
    readonly property int smsPermissionType: 6
    readonly property string windowTitleSuffix: {
        if (selectedFeature === "fileManager")
            return "File Manager"
        if (selectedFeature === "cameras")
            return "Virtual Camera"
        if (selectedFeature === "messages")
            return "Messages"
        if (selectedFeature === "notificationHistory")
            return "Notifications"
        if (selectedFeature === "settings")
            return "Settings"
        return ""
    }

    function selectFeature(featureKey) {
        const nextFeature = featureKey ? featureKey : ""

        if (nextFeature === "messages" && !root.permissionStateController.smsGranted) {
            permissionPromptDialog.permissionType = root.smsPermissionType
            permissionPromptDialog.permissionTitle = "Messages Permission Required"
            permissionPromptDialog.permissionMessage = "SMS and contacts access is disabled on the mobile app. Grant SMS permissions to use Messages."
            permissionPromptDialog.open()
            return
        }

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
            let pageUrl = ""
            let pageProperties = {}
            if (selectedFeature === "fileManager") {
                pageUrl = "qrc:/LibreConnect/desktop/FileManagerPage.qml"
            } else if (selectedFeature === "cameras") {
                pageUrl = "qrc:/LibreConnect/desktop/VirtualCameraPage.qml"
            } else if (selectedFeature === "messages") {
                pageUrl = "qrc:/LibreConnect/desktop/MessagesPage.qml"
                pageProperties = {
                    smsBridgeController: root.smsBridgeController
                }
            } else if (selectedFeature === "notificationHistory") {
                pageUrl = "qrc:/LibreConnect/desktop/NotificationHistoryPage.qml"
                pageProperties = {
                    notificationSyncController: root.notificationSyncController
                }
            } else {
                pageUrl = "qrc:/LibreConnect/desktop/SettingsPage.qml"
                pageProperties = {
                    notificationSyncController: root.notificationSyncController,
                    clipboardSyncController: root.clipboardSyncController,
                    permissionStateController: root.permissionStateController,
                    temporaryStorageController: root.temporaryStorageController
                }
            }

            if (featureStack.depth === 0)
                featureStack.push(pageUrl, pageProperties)
            else
                featureStack.replace(pageUrl, pageProperties)
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
                anchors.horizontalCenter: parent.horizontalCenter
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
                anchors.horizontalCenter: parent.horizontalCenter
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

    Dialog {
        id: findMyPhoneDialog
        title: ""
        modal: true
        closePolicy: Popup.NoAutoClose
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
            width: 340

            Text {
                text: "Ringing your phone..."
                font.family: Theme.fontFamily
                font.pixelSize: 26
                font.bold: true
                color: Theme.textColor
                wrapMode: Text.WordWrap
                width: parent.width
            }

            Text {
                text: "Tap Stop Ringing here or OK on your phone to stop the alert."
                color: Theme.textColor
                font.family: Theme.fontFamily
                font.pixelSize: 14
                wrapMode: Text.WordWrap
                width: parent.width
            }

            ThemedButton {
                text: "Stop Ringing"
                width: 160
                height: 42
                font.pixelSize: 14
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: root.connectionController.stopFindMyPhoneAlert()
            }
        }
    }

    Dialog {
        id: permissionPromptDialog
        property int permissionType: 0
        property string permissionTitle: ""
        property string permissionMessage: ""
        title: ""
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        contentWidth: 360
        background: Rectangle {
            radius: 10
            color: Theme.panelColor
            border.color: Theme.panelBorderColor
            border.width: 1
        }

        contentItem: Column {
            spacing: 14
            width: permissionPromptDialog.contentWidth

            Text {
                text: permissionPromptDialog.permissionTitle
                width: parent.width
                color: Theme.textColor
                font.family: Theme.fontFamily
                font.pixelSize: 26
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Text {
                text: permissionPromptDialog.permissionMessage
                width: parent.width
                color: Theme.textColor
                font.family: Theme.fontFamily
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 10

                ThemedButton {
                    text: "Cancel"
                    width: 120
                    height: 40
                    font.pixelSize: 14
                    onClicked: permissionPromptDialog.close()
                }

                ThemedButton {
                    text: permissionPromptDialog.permissionType >= 0 ? "Prompt on Phone" : "Allow on Mac"
                    width: 170
                    height: 40
                    font.pixelSize: 14
                    onClicked: {
                        if (permissionPromptDialog.permissionType >= 0)
                            root.permissionStateController.requestPermission(permissionPromptDialog.permissionType)
                        else
                            root.permissionStateController.requestDesktopNotificationPermission()
                        permissionPromptDialog.close()
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
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        anchors.topMargin: 8
                        anchors.bottomMargin: 8
                        spacing: 12

                        RoundedLogo {
                            source: "libreconnect_logo_1024.png"
                            Layout.preferredWidth: 62
                            Layout.preferredHeight: 62
                            Layout.alignment: Qt.AlignVCenter
                        }

                        Column {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
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

                            RowLayout {
                                spacing: 8

                                Text {
                                    id: connectedStatusLabel
                                    text: "Connected"
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 15
                                    color: Theme.mutedTextColor
                                }

                                Text {
                                    id: dotLabel
                                    visible: root.connectionController.batteryPercentage >= 0
                                    text: "•"
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 15
                                    color: Theme.mutedTextColor
                                }

                                Text {
                                    visible: root.connectionController.batteryPercentage >= 0
                                    text: root.connectionController.batteryPercentage + "%"
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 15
                                    color: Theme.subtleTextColor
                                    Layout.alignment: Qt.AlignVCenter
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                            Layout.preferredWidth: 36
                            spacing: 8

                            Rectangle {
                                id: settingsButton
                                Layout.alignment: Qt.AlignHCenter
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

                            Rectangle {
                                id: moreButton
                                Layout.alignment: Qt.AlignHCenter
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
                        const popupWidth = Math.max(actionsPopup.width, actionsPopup.implicitWidth)
                        const pt = moreButton.mapToItem(Overlay.overlay, moreButton.width - popupWidth, moreButton.height + 8)
                        x = Math.max(12, Math.min(pt.x, Overlay.overlay.width - popupWidth - 12))
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
                            color: findMyPhoneMouse.containsMouse ? Theme.buttonColor : Theme.backgroundColor
                            border.color: Theme.panelBorderColor
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: "Find My Phone"
                                font.family: Theme.fontFamily
                                font.pixelSize: 14
                                color: Theme.textColor
                            }

                            MouseArea {
                                id: findMyPhoneMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    actionsPopup.close()
                                    root.connectionController.startFindMyPhoneAlert()
                                }
                            }
                        }

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

                    Item {
                        width: parent.width
                        height: 58

                        FeatureListButton {
                            text: "Cameras"
                            iconSource: "camera.svg"
                            darkIconSource: "camera_dark.svg"
                            anchors.fill: parent
                            enabled: root.permissionStateController.cameraGranted
                            onClicked: windowRef.showVirtualCamera()
                        }

                        MouseArea {
                            anchors.fill: parent
                            visible: !root.permissionStateController.cameraGranted
                            onClicked: {
                                permissionPromptDialog.permissionType = 1
                                permissionPromptDialog.permissionTitle = "Camera Permission Required"
                                permissionPromptDialog.permissionMessage = "Camera access is disabled on the mobile app. Grant camera permission to use the Cameras feature."
                                permissionPromptDialog.open()
                            }
                        }
                    }

                    Item {
                        width: parent.width
                        height: 58

                        FeatureListButton {
                            text: "File Manager"
                            iconSource: "files.svg"
                            darkIconSource: "files_dark.svg"
                            anchors.fill: parent
                            enabled: root.permissionStateController.fileSystemGranted
                            onClicked: windowRef.showFileManager()
                        }

                        MouseArea {
                            anchors.fill: parent
                            visible: !root.permissionStateController.fileSystemGranted
                            onClicked: {
                                permissionPromptDialog.permissionType = 3
                                permissionPromptDialog.permissionTitle = "File Access Required"
                                permissionPromptDialog.permissionMessage = "File access is disabled on the mobile app. Grant file permissions to use File Manager."
                                permissionPromptDialog.open()
                            }
                        }
                    }

                    Item {
                        width: parent.width
                        height: 58

                        FeatureListButton {
                            text: "Messages"
                            iconSource: "messages.svg"
                            darkIconSource: "messages_dark.svg"
                            anchors.fill: parent
                            enabled: root.permissionStateController.smsGranted
                            onClicked: root.selectFeature("messages")
                        }

                        MouseArea {
                            anchors.fill: parent
                            visible: !root.permissionStateController.smsGranted
                            onClicked: {
                                permissionPromptDialog.permissionType = root.smsPermissionType
                                permissionPromptDialog.permissionTitle = "Messages Permission Required"
                                permissionPromptDialog.permissionMessage = "SMS and contacts access is disabled on the mobile app. Grant SMS permissions to use Messages."
                                permissionPromptDialog.open()
                            }
                        }
                    }

                    Item {
                        width: parent.width
                        height: 58

                        readonly property bool historyFeatureEnabled: root.notificationSyncController.permissionsGranted
                                                                     && root.notificationSyncController.enabled

                        FeatureListButton {
                            text: "Notifications"
                            iconSource: "notifications.svg"
                            darkIconSource: "notifications_dark.svg"
                            anchors.fill: parent
                            enabled: parent.historyFeatureEnabled
                            onClicked: root.selectFeature("notificationHistory")
                        }

                        MouseArea {
                            anchors.fill: parent
                            visible: !parent.historyFeatureEnabled
                            onClicked: {
                                if (!root.notificationSyncController.remotePermissionGranted) {
                                    permissionPromptDialog.permissionType = 2
                                    permissionPromptDialog.permissionTitle = "Phone Notification Permission Required"
                                    permissionPromptDialog.permissionMessage = "Notification access is disabled on the mobile app. Grant notification permissions to view notifications."
                                    permissionPromptDialog.open()
                                    return
                                }

                                if (!root.notificationSyncController.localPermissionGranted) {
                                    permissionPromptDialog.permissionType = -1
                                    permissionPromptDialog.permissionTitle = "macOS Notification Permission Required"
                                    permissionPromptDialog.permissionMessage = "Allow LibreConnect in System Settings > Notifications to view synced notifications on this Mac."
                                    permissionPromptDialog.open()
                                    return
                                }

                                root.selectFeature("settings")
                            }
                        }
                    }

                    Item {
                        width: parent.width
                        height: 58

                        FeatureListButton {
                            text: "Sync Clipboard"
                            iconSource: "clipboard.svg"
                            darkIconSource: "clipboard_dark.svg"
                            anchors.fill: parent
                            enabled: !root.clipboardSyncController.busy
                            onClicked: root.clipboardSyncController.syncClipboard()
                        }
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

    Connections {
        target: root.connectionController

        function onFindMyPhoneAlertActiveChanged() {
            if (root.connectionController.findMyPhoneAlertActive) {
                if (!findMyPhoneDialog.visible)
                    findMyPhoneDialog.open()
                return
            }

            if (findMyPhoneDialog.visible)
                findMyPhoneDialog.close()
        }
    }

    Component.onCompleted: {
        if (windowRef && windowRef.applyHomeWindowSize !== undefined)
            windowRef.applyHomeWindowSize()
        selectFeature(initialFeature)
        if (root.connectionController.findMyPhoneAlertActive)
            findMyPhoneDialog.open()
    }
}
