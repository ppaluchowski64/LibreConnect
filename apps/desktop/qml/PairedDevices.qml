import QtQuick
import QtQuick.Controls
import LibreConnect.desktop 1.0

Page {
    id: root

    required property var windowRef
    required property var connectionController
    readonly property string windowTitleSuffix: "Paired Devices"

    property var pairedDevices: []
    property string selectedDeviceId: ""
    property string statusMessage: ""
    property bool removingDevice: false
    property bool isConnecting: false
    property bool connectionAttemptActive: false
    property var onlineDeviceIds: ({})
    readonly property int connectAttemptTimeoutMs: 12000

    DeviceDiscovery {
        id: discovery
    }

    function isDeviceOnline(deviceId) {
        return onlineDeviceIds[deviceId] === true
    }

    function sortPairedDevices() {
        if (!pairedDevices || pairedDevices.length <= 1) {
            return
        }

        const selectedId = selectedDeviceId
        const sorted = pairedDevices.slice().sort(function(lhs, rhs) {
            const lhsOnline = isDeviceOnline(lhs.deviceId)
            const rhsOnline = isDeviceOnline(rhs.deviceId)
            if (lhsOnline !== rhsOnline) {
                return lhsOnline ? -1 : 1
            }

            const lhsName = (lhs.deviceName || "").toLowerCase()
            const rhsName = (rhs.deviceName || "").toLowerCase()
            if (lhsName < rhsName) {
                return -1
            }
            if (lhsName > rhsName) {
                return 1
            }

            return lhs.deviceId < rhs.deviceId ? -1 : (lhs.deviceId > rhs.deviceId ? 1 : 0)
        })

        pairedDevices = sorted

        if (selectedId && selectedId.length > 0) {
            selectedDeviceId = selectedId
        } else if (pairedDevices.length > 0) {
            selectedDeviceId = pairedDevices[0].deviceId
        }
    }

    function rebuildOnlineStatus() {
        const updated = {}
        const rows = discovery.model.rowCount()
        for (let i = 0; i < rows; ++i) {
            const device = discovery.deviceAt(i)
            if (device && device.deviceId)
                updated[device.deviceId] = true
        }

        onlineDeviceIds = updated
        sortPairedDevices()
    }

    function reloadPairedDevices() {
        pairedDevices = connectionController.getPairedDevices()

        if (pairedDevices.length === 0) {
            selectedDeviceId = ""
            windowRef.showInitial()
            return
        }

        let hasSelectedDevice = false
        for (let i = 0; i < pairedDevices.length; ++i) {
            if (pairedDevices[i].deviceId === selectedDeviceId) {
                hasSelectedDevice = true
                break
            }
        }

        if (!selectedDeviceId || !hasSelectedDevice) {
            selectedDeviceId = pairedDevices[0].deviceId
        }

        sortPairedDevices()
    }

    function startDiscovery() {
        if (Qt.platform.os === "osx" && !connectionController.localNetworkPermissionGranted) {
            localNetworkDialog.open()
            return
        }

        discovery.discover()
    }

    function selectedDeviceName() {
        for (let i = 0; i < pairedDevices.length; ++i) {
            const item = pairedDevices[i]
            if (item.deviceId === selectedDeviceId)
                return item.deviceName
        }
        return "Connected Device"
    }

    function connectSelectedDevice() {
        if (!selectedDeviceId || isConnecting || connectionController.pending || connectionController.connected)
            return

        if (!isDeviceOnline(selectedDeviceId)) {
            statusMessage = "Selected device is currently offline. Open the mobile app and try again."
            return
        }

        const discoveredDevice = discovery.deviceById(selectedDeviceId)
        if (!discoveredDevice || !discoveredDevice.ipAddress || discoveredDevice.port <= 0) {
            statusMessage = "Selected device is currently offline. Open the mobile app and try again."
            return
        }

        statusMessage = ""
        connectionAttemptActive = true
        isConnecting = true
        connectAttemptTimer.restart()
        windowRef.prepareConnection(selectedDeviceId, selectedDeviceName(), false)
        connectionController.connectTo(discoveredDevice.ipAddress, discoveredDevice.port, 1)
    }

    function removeSelectedDevice() {
        if (!selectedDeviceId || removingDevice)
            return

        removingDevice = true
        const removed = connectionController.removePairedDevice(selectedDeviceId)
        removingDevice = false

        if (!removed) {
            statusMessage = "Could not remove the selected paired device."
            return
        }

        statusMessage = ""
        reloadPairedDevices()
    }

    background: Rectangle {
        color: Theme.backgroundColor
    }

    RoundedLogo {
        id: logo
        source: "qrc:/LibreConnect/desktop/libreconnect_logo_1024.png"
        width: 140
        height: 140
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 24
    }

    Text {
        id: title
        text: "Paired Devices"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 24
        font.family: Theme.fontFamily
        font.pixelSize: 36
        color: Theme.textColor
    }

    Text {
        text: "Select a paired device to connect, or remove devices you no longer use."
        anchors.left: title.left
        anchors.right: logo.left
        anchors.top: title.bottom
        anchors.topMargin: 8
        wrapMode: Text.WordWrap
        font.family: Theme.fontFamily
        color: Theme.mutedTextColor
        font.pixelSize: 16
    }

    Item {
        id: contentArea
        anchors.top: title.bottom
        anchors.topMargin: 24
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24

        Item {
            id: contentRow
            anchors.left: parent.left
            anchors.leftMargin: 24
            anchors.right: parent.right
            anchors.rightMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            height: 292

            Rectangle {
                id: pairedListContainer
                x: 0
                y: 8
                width: contentRow.width - actionColumn.width - 20
                height: 250
                border.color: Theme.panelBorderColor
                border.width: 1
                color: Theme.panelColor

                ListView {
                    id: pairedList
                    anchors.fill: parent
                    model: pairedDevices
                    clip: true
                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }

                    delegate: Rectangle {
                        id: rowItem
                        required property var modelData
                        property bool selected: root.selectedDeviceId === modelData.deviceId
                        property bool online: root.isDeviceOnline(modelData.deviceId)

                        width: pairedList.width
                        height: 80
                        color: selected ? Theme.selectedColor : Theme.backgroundColor
                        border.color: selected ? Theme.selectedBorderColor : Theme.panelBorderColor
                        border.width: selected ? 2 : 1

                        Row {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 12

                            Image {
                                source: "android.png"
                                width: 48
                                height: 48
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - 72
                                spacing: 4

                                Text {
                                    text: rowItem.modelData.deviceName
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 18
                                    font.bold: true
                                    color: Theme.textColor
                                }

                                Text {
                                    text: rowItem.modelData.deviceType + "  |  " + rowItem.modelData.deviceId
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 12
                                    color: Theme.mutedTextColor
                                    elide: Text.ElideRight
                                }

                                Row {
                                    spacing: 6

                                    Rectangle {
                                        width: 10
                                        height: 10
                                        radius: 5
                                        color: rowItem.online ? Theme.successColor : Theme.subtleTextColor
                                        anchors.verticalCenter: parent.verticalCenter
                                    }

                                    Text {
                                        text: rowItem.online ? "Online" : "Offline"
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        color: rowItem.online ? Theme.successColor : Theme.subtleTextColor
                                    }
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.selectedDeviceId = rowItem.modelData.deviceId
                            onDoubleClicked: {
                                root.selectedDeviceId = rowItem.modelData.deviceId
                                root.connectSelectedDevice()
                            }
                        }
                    }
                }
            }

            Column {
                id: actionColumn
                x: pairedListContainer.width + 20
                width: 180
                spacing: 12
                anchors.bottom: pairedListContainer.bottom

                ThemedButton {
                    text: isConnecting || connectionController.pending ? "Connecting..." : "Connect"
                    width: parent.width
                    height: 48
                    enabled: root.selectedDeviceId.length > 0
                             && root.isDeviceOnline(root.selectedDeviceId)
                             && !isConnecting
                             && !connectionController.pending
                             && !connectionController.connected
                    onClicked: root.connectSelectedDevice()
                }

                ThemedButton {
                    text: removingDevice ? "Removing..." : "Remove"
                    width: parent.width
                    height: 48
                    enabled: root.selectedDeviceId.length > 0 && !removingDevice && !isConnecting
                    onClicked: root.removeSelectedDevice()
                }

                ThemedButton {
                    text: "New Pairing"
                    width: parent.width
                    height: 48
                    enabled: !isConnecting && !removingDevice
                    onClicked: windowRef.showDevicePicker(true)
                }
            }

            Text {
                anchors.left: parent.left
                anchors.right: pairedListContainer.right
                anchors.top: pairedListContainer.bottom
                anchors.topMargin: 10
                text: statusMessage
                color: Theme.dangerColor
                font.family: Theme.fontFamily
                font.pixelSize: 13
                visible: statusMessage.length > 0
                wrapMode: Text.WordWrap
            }
        }
    }

    Connections {
        target: connectionController

        function onConnectedChanged() {
            if (connectionController.connected) {
                connectAttemptTimer.stop()
                isConnecting = false
                connectionAttemptActive = false
                discovery.cancelScan()
                return
            }

            if (connectionAttemptActive) {
                connectAttemptTimer.stop()
                isConnecting = false
                connectionAttemptActive = false
            }
        }

        function onPendingChanged() {
            isConnecting = connectionController.pending
            if (!connectionController.pending) {
                connectAttemptTimer.stop()
                connectionAttemptActive = false
            }
        }

        function onLastErrorChanged() {
            if (connectionController.lastError.length > 0) {
                connectAttemptTimer.stop()
                statusMessage = connectionController.lastError
                isConnecting = false
                connectionAttemptActive = false
            }
        }

        function onPairedDevicesChanged() {
            reloadPairedDevices()
        }
    }

    Timer {
        id: connectAttemptTimer
        interval: root.connectAttemptTimeoutMs
        repeat: false
        onTriggered: {
            if (root.connectionAttemptActive || root.isConnecting || connectionController.pending) {
                connectionController.disconnect()
                root.isConnecting = false
                root.connectionAttemptActive = false
                root.statusMessage = "Connection timed out before handshake completed. Verify both devices are paired and try again."
            }
        }
    }

    Connections {
        target: discovery.model

        function onDataChanged() {
            rebuildOnlineStatus()
        }

        function onRowsInserted() {
            rebuildOnlineStatus()
        }

        function onRowsRemoved() {
            rebuildOnlineStatus()
        }

        function onModelReset() {
            rebuildOnlineStatus()
        }
    }

    Dialog {
        id: localNetworkDialog
        modal: true
        title: ""
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 430
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: 10
            color: Theme.panelColor
            border.color: Theme.panelBorderColor
            border.width: 1
        }

        Column {
            width: parent.width
            spacing: 12

            Text {
                width: parent.width
                text: "Allow Local Network Access"
                font.family: Theme.fontFamily
                font.pixelSize: 26
                font.bold: true
                color: Theme.textColor
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                text: "LibreConnect needs local network access before it can look for your paired phones on macOS."
                font.family: Theme.fontFamily
                font.pixelSize: 16
                color: Theme.textColor
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                text: "Continue to show the system permission prompt. Until it is granted, online status checks will stay paused."
                font.family: Theme.fontFamily
                font.pixelSize: 14
                color: Theme.mutedTextColor
                wrapMode: Text.WordWrap
            }

            Row {
                spacing: 10

                ThemedButton {
                    text: "Not Now"
                    width: 140
                    height: 44
                    onClicked: localNetworkDialog.close()
                }

                ThemedButton {
                    text: "Continue"
                    width: 140
                    height: 44
                    onClicked: {
                        localNetworkDialog.close()
                        discovery.discover()
                    }
                }
            }
        }
    }

    Component.onCompleted: {
        reloadPairedDevices()
        startDiscovery()
        rebuildOnlineStatus()
    }
}
