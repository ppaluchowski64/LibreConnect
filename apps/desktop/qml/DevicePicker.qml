import QtQuick
import QtQuick.Controls
import LibreConnect.desktop 1.0

Page {
    id: devicePicker

    required property var windowRef
    required property var connectionController
    property bool allowBackToPairedDevices: false
    readonly property string windowTitleSuffix: "Setup"

    DeviceDiscovery {
        id: discovery
    }

    property bool isConnecting: false
    property bool connectionAttemptActive: false

    background: Rectangle {
        color: Theme.backgroundColor
    }

    function connectToCurrent() {
        if (deviceListView.currentIndex < 0 || isConnecting)
            return

        const dev = discovery.deviceAt(deviceListView.currentIndex)
        windowRef.prepareConnection(dev.deviceId, dev.deviceName, !connectionController.hasPairedDevices)
        connectionAttemptActive = true
        connectionController.connectTo(dev.ipAddress, dev.port, 0)
        isConnecting = true
    }

    function endConnectionAttempt(showBackTarget) {
        isConnecting = false
        connectionAttemptActive = false
        if (showBackTarget && allowBackToPairedDevices) {
            windowRef.showPairedDevices()
        }
    }

    Image {
        id: logo
        source: "libreconnect_logo.png"
        width: 140
        height: 140
        fillMode: Image.PreserveAspectFit
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 24
    }

    Rectangle {
        id: deviceListContainer
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 20
        width: 400
        border.color: Theme.panelBorderColor
        border.width: 1
        color: Theme.panelColor

        ListView {
            id: deviceListView
            anchors.fill: parent
            clip: true
            focus: true
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            model: discovery.model

            highlight: Rectangle {
                color: Theme.selectedColor
                border.color: Theme.selectedBorderColor
                border.width: 1
                radius: 2
                visible: deviceListView.currentIndex >= 0
            }
            highlightFollowsCurrentItem: true
            highlightMoveDuration: 120

            delegate: Rectangle {
                id: tile
                width: deviceListView.width
                height: 100
                radius: 4
                property bool pressed: false

                color: pressed ? Qt.darker(Theme.selectedColor, 1.08) : (ListView.isCurrentItem ? Theme.selectedColor : Theme.backgroundColor)
                border.color: ListView.isCurrentItem ? Theme.selectedBorderColor : Theme.panelBorderColor
                border.width: ListView.isCurrentItem ? 2 : 1

                Row {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    Image {
                        id: deviceIcon
                        source: icon
                        width: 48
                        height: 48
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4

                        Text {
                            text: deviceName
                            font.family: Theme.fontFamily
                            font.pixelSize: 18
                            font.bold: true
                            color: Theme.textColor
                        }

                        Text {
                            text: "IP: " + ipAddress + ", " + osName + " " + osVersion
                            font.family: Theme.fontFamily
                            font.pixelSize: 14
                            color: Theme.mutedTextColor
                        }

                        Text {
                            text: "App version " + appVersion
                            font.family: Theme.fontFamily
                            font.pixelSize: 14
                            color: Theme.mutedTextColor
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: deviceListView.currentIndex = index
                    onDoubleClicked: {
                        deviceListView.currentIndex = index
                        devicePicker.connectToCurrent()
                    }
                }
            }

            footer: Text {
                width: deviceListView.width
                topPadding: 20
                horizontalAlignment: Text.AlignHCenter
                font.family: Theme.fontFamily
                font.pixelSize: 16
                color: Theme.subtleTextColor
                visible: deviceListView.count === 0
                text: discovery.searching ? "Searching for devices..." : "No devices found"
            }

            Keys.onReturnPressed: devicePicker.connectToCurrent()
            Keys.onEnterPressed: devicePicker.connectToCurrent()
            Keys.onEscapePressed: deviceListView.currentIndex = -1
        }
    }

    Column {
        id: buttonColumn
        anchors.top: deviceListContainer.top
        anchors.right: logo.left
        anchors.rightMargin: 20
        spacing: 12

        ThemedButton {
            id: backButton
            text: "Back"
            width: 120
            height: 48
            font.pixelSize: 16
            visible: allowBackToPairedDevices
            onClicked: windowRef.showPairedDevices()
        }

        ThemedButton {
            id: connectButton
            text: isConnecting ? "Connecting..." : "Connect"
            width: 120
            height: 48
            font.pixelSize: 16
            enabled: deviceListView.currentIndex >= 0
                     && !connectionController.pending
                     && !connectionController.connected
            onClicked: devicePicker.connectToCurrent()
        }

        ThemedButton {
            id: refreshButton
            text: "Refresh"
            width: 120
            height: 48
            font.pixelSize: 16
            enabled: !isConnecting
            onClicked: discovery.discover()
        }
    }

    Column {
        id: helpText
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 24
        anchors.topMargin: 100
        anchors.top: logo.bottom
        width: 280

        Text {
            text: "If your phone isn't appearing:"
            font.family: Theme.fontFamily
            font.pixelSize: 16
            font.bold: true
            color: Theme.textColor
            width: parent.width
        }

        Column {
            width: parent.width

            Text {
                text: "\u2022 Make sure both of your devices are on the same network"
                font.family: Theme.fontFamily
                font.pixelSize: 14
                color: Theme.mutedTextColor
                wrapMode: Text.WordWrap
                width: parent.width - 20
            }

            Text {
                text: "\u2022 Make sure the app is in the foreground"
                font.family: Theme.fontFamily
                font.pixelSize: 14
                color: Theme.mutedTextColor
                wrapMode: Text.WordWrap
                width: parent.width - 20
            }
        }
    }

    Connections {
        target: connectionController

        function onConnectedChanged() {
            if (connectionController.connected) {
                endConnectionAttempt(false)
                discovery.cancelScan()
                return
            }

            if (connectionAttemptActive || verificationDialog.visible) {
                endConnectionAttempt(true)
            }
        }

        function onLastErrorChanged() {
            if (connectionController.lastError.length > 0) {
                endConnectionAttempt(true)
            }
        }

        function onPendingChanged() {
            isConnecting = connectionController.pending
        }

        function onVerificationPendingChanged() {
            if (connectionController.verificationPending) {
                verificationCodeField.text = ""
                verificationDialog.open()
            } else if (verificationDialog.visible) {
                verificationDialog.close()
                if (!connectionController.connected && !connectionController.pending) {
                    endConnectionAttempt(true)
                }
            }
        }
    }

    Dialog {
        id: verificationDialog
        modal: true
        title: ""
        closePolicy: Popup.NoAutoClose
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        background: Rectangle {
            radius: 10
            color: Theme.panelColor
            border.color: Theme.panelBorderColor
            border.width: 1
        }

        Column {
            spacing: 12
            width: 360

            Text {
                text: "Verify connection"
                font.family: Theme.fontFamily
                font.pixelSize: 26
                font.bold: true
                color: Theme.textColor
            }

            Text {
                text: "Enter the pairing code shown on your phone to approve this connection."
                font.family: Theme.fontFamily
                color: Theme.textColor
                wrapMode: Text.WordWrap
                width: parent.width
            }

            TextField {
                id: verificationCodeField
                placeholderText: "6-digit code"
                inputMethodHints: Qt.ImhDigitsOnly
                maximumLength: 6
                validator: RegularExpressionValidator { regularExpression: /\d{0,6}/ }
                width: parent.width
                color: Theme.textColor
                placeholderTextColor: Theme.subtleTextColor
                background: Rectangle {
                    radius: 6
                    color: Theme.backgroundColor
                    border.color: Theme.selectedBorderColor
                    border.width: 1
                }
                onAccepted: {
                    if (verificationCodeField.text.length === 6) {
                        connectionController.submitVerificationCode(verificationCodeField.text)
                    }
                }
            }

            Text {
                visible: connectionController.verificationTriesLeft > 0
                text: "Tries left: " + connectionController.verificationTriesLeft
                font.family: Theme.fontFamily
                color: Theme.subtleTextColor
            }

            Text {
                visible: connectionController.verificationError.length > 0
                text: connectionController.verificationError
                font.family: Theme.fontFamily
                color: Theme.dangerColor
                wrapMode: Text.WordWrap
                width: parent.width
            }

            Row {
                spacing: 10

                ThemedButton {
                    text: "Submit"
                    enabled: verificationCodeField.text.length === 6
                    onClicked: connectionController.submitVerificationCode(verificationCodeField.text)
                }

                ThemedButton {
                    text: "Cancel"
                    onClicked: connectionController.cancelVerification()
                }
            }
        }
    }

    Component.onCompleted: {
        discovery.discover()
        console.log("DevicePicker loaded OK")
    }
}
