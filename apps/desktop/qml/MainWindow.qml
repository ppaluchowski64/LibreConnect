import QtQuick
import QtQuick.Controls
import LibreConnect.desktop 1.0

Window {
    id: window
    readonly property int standardWindowFlags: Qt.Window
                                              | Qt.WindowTitleHint
                                              | Qt.WindowSystemMenuHint
                                              | Qt.WindowMinimizeButtonHint
                                              | Qt.WindowMaximizeButtonHint
                                              | Qt.WindowCloseButtonHint
    flags: standardWindowFlags
    property bool startupConnectionPending: false
    visible: !startupConnectionPending
    width: 740
    height: 420
    property bool homeMode: false
    minimumWidth: 740
    minimumHeight: 420
    maximumWidth: 740
    maximumHeight: 420
    color: Theme.backgroundColor
    property string baseWindowTitle: "LibreConnect"
    property string currentWindowTitleSuffix: ""
    property string activeDeviceName: ""
    property string activeDeviceId: ""
    property bool showConnectedAfterConnect: false
    readonly property int fixedWindowWidth: 740
    readonly property int fixedWindowHeight: 420
    readonly property int homeWindowWidth: 1280
    readonly property int homeWindowHeight: 800
    readonly property int homeMinimumWindowWidth: 960
    readonly property int homeMinimumWindowHeight: 660
    readonly property bool isMacOS: Qt.platform.os === "osx"
    readonly property bool isLinux: Qt.platform.os === "linux"
    readonly property bool virtualCameraAvailable: VirtualCameraAvailable
    readonly property string virtualCameraUnavailableReason: VirtualCameraUnavailableReason
    title: currentWindowTitleSuffix.length > 0
           ? baseWindowTitle + " - " + currentWindowTitleSuffix
           : baseWindowTitle

    DeviceConnectionController {
        id: connectionController
    }

    NotificationSyncController {
        id: notificationSyncController
    }

    ClipboardSyncController {
        id: clipboardSyncController
    }

    SmsBridgeController {
        id: smsBridgeController
    }

    PermissionStateController {
        id: permissionStateController
    }

    TemporaryStorageController {
        id: temporaryStorageController
    }

    MediaNotificationController {
        id: mediaNotificationController
    }

    function updateWindowTitle() {
        const currentItem = stackView.currentItem
        currentWindowTitleSuffix = currentItem && currentItem.windowTitleSuffix !== undefined
                ? currentItem.windowTitleSuffix
                : ""
    }

    function replaceRootPage(url, properties) {
        if (stackView.depth === 0) {
            stackView.push(url, properties)
            return
        }

        if (stackView.depth > 1) {
            stackView.clear()
            stackView.push(url, properties)
            return
        }

        stackView.replace(url, properties)
    }

    function applyFixedWindowSize() {
        if (flags !== standardWindowFlags) {
            flags = standardWindowFlags
        }

        minimumWidth = fixedWindowWidth
        minimumHeight = fixedWindowHeight
        maximumWidth = fixedWindowWidth
        maximumHeight = fixedWindowHeight
        
        homeMode = false
        width = fixedWindowWidth
        height = fixedWindowHeight
    }

    function applyHomeWindowSize() {
        const enteringHomeMode = !homeMode

        if (!isLinux && enteringHomeMode) {
            flags = standardWindowFlags & ~Qt.WindowMaximizeButtonHint
        }

        maximumWidth = 16777215
        maximumHeight = 16777215
        minimumWidth = homeMinimumWindowWidth
        minimumHeight = homeMinimumWindowHeight

        if (flags !== standardWindowFlags) {
            flags = standardWindowFlags
        }
        
        homeMode = true
        if (enteringHomeMode) {
            width = Math.max(homeWindowWidth, homeMinimumWindowWidth)
            height = Math.max(homeWindowHeight, homeMinimumWindowHeight)
        }
    }

    function showInitial() {
        applyFixedWindowSize()
        replaceRootPage("qrc:/LibreConnect/desktop/Initial.qml", {
            windowRef: window,
            connectionController: connectionController
        })
    }

    function showPairedDevices() {
        applyFixedWindowSize()
        replaceRootPage("qrc:/LibreConnect/desktop/PairedDevices.qml", {
            windowRef: window,
            connectionController: connectionController
        })
    }

    function showDevicePicker(allowBackToPairedDevices) {
        applyFixedWindowSize()
        replaceRootPage("qrc:/LibreConnect/desktop/DevicePicker.qml", {
            windowRef: window,
            connectionController: connectionController,
            allowBackToPairedDevices: allowBackToPairedDevices === true
        })
    }

    function showHome() {
        showHomeWithFeature("")
    }

    function showHomeWithFeature(initialFeature) {
        applyHomeWindowSize()
        replaceRootPage("qrc:/LibreConnect/desktop/Home.qml", {
            windowRef: window,
            connectionController: connectionController,
            activeDeviceName: activeDeviceName,
            activeDeviceId: activeDeviceId,
            notificationSyncController: notificationSyncController,
            mediaNotificationController: mediaNotificationController,
            clipboardSyncController: clipboardSyncController,
            smsBridgeController: smsBridgeController,
            permissionStateController: permissionStateController,
            temporaryStorageController: temporaryStorageController,
            virtualCameraAvailable: virtualCameraAvailable,
            virtualCameraUnavailableReason: virtualCameraUnavailableReason,
            initialFeature: initialFeature === undefined ? "" : initialFeature
        })
    }

    function showConnected() {
        applyFixedWindowSize()
        replaceRootPage("qrc:/LibreConnect/desktop/Connected.qml", {
            windowRef: window,
            activeDeviceName: activeDeviceName
        })
    }

    function showFeature(featureKey) {
        const currentItem = stackView.currentItem
        if (currentItem && currentItem.selectFeature !== undefined) {
            currentItem.selectFeature(featureKey)
            return
        }

        showHomeWithFeature(featureKey)
    }

    function showFileManager() {
        showFeature("fileManager")
    }

    function showSettings() {
        showFeature("settings")
    }

    function showVirtualCamera() {
        if (!virtualCameraAvailable) {
            virtualCameraUnsupportedDialog.open()
            return
        }

        showFeature("cameras")
    }

    function showVirtualMicrophone() {
        showFeature("microphone")
    }

    function showMessages() {
        showFeature("messages")
    }

    function showMediaRemote() {
        showFeature("mediaRemote")
    }

    function goBack() {
        if (stackView.depth > 1)
            stackView.pop()
    }

    function prepareConnection(deviceId, deviceName, firstPairing) {
        activeDeviceId = deviceId ? deviceId : ""
        activeDeviceName = deviceName ? deviceName : qsTr("Connected Device")
        showConnectedAfterConnect = firstPairing === true
    }

    StackView {
        id: stackView
        anchors.fill: parent

        pushEnter: Transition {
            PropertyAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: 300
            }
            PropertyAnimation {
                property: "x"
                from: stackView.width
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
                to: -stackView.width * 0.3
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
                from: stackView.width
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
                to: -stackView.width * 0.3
                duration: 300
                easing.type: Easing.OutCubic
            }
        }

        popEnter: Transition {
            PropertyAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: 300
            }
            PropertyAnimation {
                property: "x"
                from: -stackView.width * 0.3
                to: 0
                duration: 300
                easing.type: Easing.OutCubic
            }
        }

        popExit: Transition {
            PropertyAnimation {
                property: "opacity"
                from: 1
                to: 0
                duration: 300
            }
            PropertyAnimation {
                property: "x"
                from: 0
                to: stackView.width
                duration: 300
                easing.type: Easing.OutCubic
            }
        }

        onCurrentItemChanged: window.updateWindowTitle()
    }

    Connections {
        target: connectionController

        function onConnectedChanged() {
            if (connectionController.connected) {
                if (connectionController.connectedDeviceId !== "")
                    activeDeviceId = connectionController.connectedDeviceId
                if (showConnectedAfterConnect) {
                    showConnectedAfterConnect = false
                    showConnected()
                } else {
                    showHome()
                }
                if (window.visible && activeDeviceId !== "" && connectionController.autoConnectPromptEnabled
                        && !connectionController.autoConnectEnabled(activeDeviceId)) {
                    autoConnectDialog.deviceId = activeDeviceId
                    autoConnectDialog.open()
                }
                return
            }

            autoConnectDialog.close()

            if (stackView.depth > 0) {
                activeDeviceName = ""
                activeDeviceId = ""
                connectionController.refreshPairedDevices()
                if (connectionController.hasPairedDevices) {
                    showPairedDevices()
                } else {
                    showInitial()
                }
            }
        }
    }

    Dialog {
        id: autoConnectDialog
        property string deviceId: ""
        title: ""
        modal: true
        anchors.centerIn: Overlay.overlay
        closePolicy: Popup.CloseOnEscape
        padding: 22
        onOpened: neverAskAgain.checked = false
        onAccepted: {
            if (neverAskAgain.checked)
                connectionController.autoConnectPromptEnabled = false
            if (!connectionController.setAutoConnectEnabled(deviceId, true))
                autoConnectErrorDialog.open()
        }
        onRejected: {
            if (neverAskAgain.checked)
                connectionController.autoConnectPromptEnabled = false
        }
        background: Rectangle {
            radius: 10
            color: Theme.panelColor
            border.color: Theme.panelBorderColor
            border.width: 1
        }
        contentItem: Column {
            width: 360
            spacing: 16
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily
                font.pixelSize: 24
                font.bold: true
                color: Theme.textColor
                text: qsTr("Connect automatically?")
            }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily
                font.pixelSize: 15
                color: Theme.mutedTextColor
                text: qsTr("Connect this paired device automatically whenever it is available.")
            }
            CheckBox {
                id: neverAskAgain
                width: parent.width
                spacing: 10
                text: qsTr("Don't ask again")
                font.family: Theme.fontFamily
                font.pixelSize: 14
                indicator: Rectangle {
                    implicitWidth: 20
                    implicitHeight: 20
                    x: neverAskAgain.leftPadding
                    y: (neverAskAgain.height - height) / 2
                    radius: 4
                    color: neverAskAgain.checked ? Theme.selectedColor : Theme.buttonColor
                    border.color: neverAskAgain.checked ? Theme.selectedBorderColor : Theme.panelBorderColor
                    border.width: 1
                    Text {
                        anchors.centerIn: parent
                        visible: neverAskAgain.checked
                        text: "✓"
                        font.pixelSize: 15
                        font.bold: true
                        color: Theme.textColor
                    }
                }
                contentItem: Text {
                    leftPadding: neverAskAgain.indicator.width + neverAskAgain.spacing
                    text: neverAskAgain.text
                    font: neverAskAgain.font
                    color: Theme.textColor
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 10
                ThemedButton {
                    text: qsTr("Not now")
                    width: 160
                    height: 42
                    font.pixelSize: 14
                    onClicked: autoConnectDialog.reject()
                }
                ThemedButton {
                    text: qsTr("Enable")
                    width: 160
                    height: 42
                    font.pixelSize: 14
                    onClicked: autoConnectDialog.accept()
                }
            }
        }
    }

    Dialog {
        id: autoConnectErrorDialog
        title: ""
        modal: true
        anchors.centerIn: Overlay.overlay
        padding: 22
        background: Rectangle {
            radius: 10
            color: Theme.panelColor
            border.color: Theme.panelBorderColor
        }
        contentItem: Column {
            width: 340
            spacing: 16
            Text {
                text: qsTr("Auto-connect unavailable")
                font.family: Theme.fontFamily
                font.pixelSize: 22
                font.bold: true
                color: Theme.textColor
            }
            Text {
                width: parent.width
                text: connectionController.lastError
                font.family: Theme.fontFamily
                font.pixelSize: 14
                color: Theme.textColor
                wrapMode: Text.WordWrap
            }
            ThemedButton {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("OK")
                width: 160
                height: 42
                onClicked: autoConnectErrorDialog.close()
            }
        }
    }

    Dialog {
        id: virtualCameraUnsupportedDialog
        title: qsTr("Virtual Camera Unavailable")
        modal: true
        standardButtons: Dialog.Ok
        anchors.centerIn: Overlay.overlay
        contentWidth: 320
        contentHeight: virtualCameraUnsupportedText.implicitHeight

        contentItem: Text {
            id: virtualCameraUnsupportedText
            text: virtualCameraUnavailableReason
            width: virtualCameraUnsupportedDialog.contentWidth
            color: Theme.textColor
            font.family: Theme.fontFamily
            font.pixelSize: 14
            wrapMode: Text.WordWrap
        }
    }

    Component.onCompleted: {
        if (startupConnectionPending)
            return

        connectionController.refreshPairedDevices()
        if (connectionController.hasPairedDevices) {
            showPairedDevices()
        } else {
            showInitial()
        }
    }
}
