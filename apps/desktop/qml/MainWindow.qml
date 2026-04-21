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
    visible: true
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
    readonly property bool isMacOS: Qt.platform.os === "osx"
    title: currentWindowTitleSuffix.length > 0
           ? baseWindowTitle + " - " + currentWindowTitleSuffix
           : baseWindowTitle

    DeviceConnectionController {
        id: connectionController
    }

    NotificationSyncController {
        id: notificationSyncController
    }

    PermissionStateController {
        id: permissionStateController
    }

    TemporaryStorageController {
        id: temporaryStorageController
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
        flags = standardWindowFlags
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
        flags = standardWindowFlags & ~Qt.WindowMaximizeButtonHint
        flags = standardWindowFlags
        maximumWidth = 16777215
        maximumHeight = 16777215
        minimumWidth = 0
        minimumHeight = 0
        minimumWidth = 860
        minimumHeight = 560
        homeMode = true
        if (enteringHomeMode) {
            width = 1280
            height = 720
        }
    }

    function showInitial() {
        applyFixedWindowSize()
        replaceRootPage("qrc:/LibreConnect/desktop/Initial.qml", {
            windowRef: window
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
            permissionStateController: permissionStateController,
            temporaryStorageController: temporaryStorageController,
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
        if (isMacOS) {
            virtualCameraUnsupportedDialog.open()
            return
        }

        showFeature("cameras")
    }

    function goBack() {
        if (stackView.depth > 1)
            stackView.pop()
    }

    function prepareConnection(deviceId, deviceName, firstPairing) {
        activeDeviceId = deviceId ? deviceId : ""
        activeDeviceName = deviceName ? deviceName : "Connected Device"
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
                if (showConnectedAfterConnect) {
                    showConnectedAfterConnect = false
                    showConnected()
                } else {
                    showHome()
                }
                return
            }

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
        id: virtualCameraUnsupportedDialog
        title: "Virtual Camera Unavailable"
        modal: true
        standardButtons: Dialog.Ok
        anchors.centerIn: Overlay.overlay
        contentWidth: 320
        contentHeight: virtualCameraUnsupportedText.implicitHeight

        contentItem: Text {
            id: virtualCameraUnsupportedText
            text: "Virtual Camera is not supported on macOS."
            width: virtualCameraUnsupportedDialog.contentWidth
            color: Theme.textColor
            font.family: Theme.fontFamily
            font.pixelSize: 14
            wrapMode: Text.WordWrap
        }
    }

    Component.onCompleted: {
        connectionController.refreshPairedDevices()
        if (connectionController.hasPairedDevices) {
            showPairedDevices()
        } else {
            showInitial()
        }
    }
}
