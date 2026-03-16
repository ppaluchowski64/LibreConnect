import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Page {
    id: root

    required property var windowRef
    readonly property string windowTitleSuffix: "File Manager"

    FileManagerController {
        id: fileManagerController
    }

    background: Rectangle {
        color: "white"
    }

    FolderDialog {
        id: folderDialog
        title: "Choose download folder"
        onAccepted: {
            if (selectedFolder)
                fileManagerController.setLocalDownloadDirectory(selectedFolder)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 20

        Row {
            Layout.fillWidth: true
            spacing: 16

            Button {
                text: "Back"
                width: 100
                height: 42
                onClicked: windowRef.goBack()
            }

            Text {
                text: "File Manager"
                font.pixelSize: 30
                font.bold: true
                color: "#111111"
                verticalAlignment: Text.AlignVCenter
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: controlsColumn.implicitHeight + 36
            radius: 12
            color: "#f4f4f4"
            border.color: "#d8d8d8"

            Column {
                id: controlsColumn
                anchors.fill: parent
                anchors.margins: 18
                spacing: 14

                Text {
                    text: "Browse the connected device and download files or folders locally."
                    font.pixelSize: 18
                    color: "#111111"
                    wrapMode: Text.WordWrap
                    width: parent.width
                }

                Row {
                    width: parent.width
                    spacing: 10

                    TextField {
                        id: pathField
                        width: parent.width - 230
                        text: fileManagerController.currentRemotePath
                        placeholderText: "Remote path"
                        onAccepted: fileManagerController.browseTo(text)
                    }

                    Button {
                        text: "Up"
                        width: 70
                        onClicked: fileManagerController.goUp()
                    }

                    Button {
                        text: "Refresh"
                        width: 120
                        onClicked: fileManagerController.refreshEntries()
                    }
                }

                Row {
                    width: parent.width
                    spacing: 12

                    Button {
                        text: "Choose Download Folder"
                        width: 220
                        onClicked: folderDialog.open()
                    }

                    Text {
                        text: fileManagerController.localDownloadDirectory
                        width: parent.width - 240
                        wrapMode: Text.WordWrap
                        color: "#444444"
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 180
            radius: 12
            color: "#fafafa"
            border.color: "#d8d8d8"

            ListView {
                id: entriesView
                anchors.fill: parent
                anchors.margins: 10
                clip: true
                spacing: 8
                model: fileManagerController.remoteEntries

                delegate: Rectangle {
                    width: entriesView.width
                    height: 58
                    radius: 10
                    color: modelData.isDirectory ? "#eef5ff" : "#f2f2f2"
                    border.color: modelData.isDirectory ? "#bfd3f2" : "#d8d8d8"

                    Row {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 12

                        Text {
                            text: modelData.isDirectory ? "Folder" : "File"
                            width: 60
                            color: "#444444"
                            verticalAlignment: Text.AlignVCenter
                        }

                        Column {
                            width: parent.width - 170
                            spacing: 2

                            Text {
                                text: modelData.name
                                font.pixelSize: 16
                                font.bold: true
                                color: "#111111"
                                elide: Text.ElideRight
                                width: parent.width
                            }

                            Text {
                                text: modelData.path
                                font.pixelSize: 12
                                color: "#666666"
                                elide: Text.ElideRight
                                width: parent.width
                            }
                        }

                        Button {
                            width: 74
                            text: modelData.isDirectory ? "Open" : "Get"
                            onClicked: {
                                if (modelData.isDirectory)
                                    fileManagerController.browseTo(modelData.path)
                                else
                                    fileManagerController.downloadEntry(modelData.path)
                            }
                        }
                    }
                }
            }

            Column {
                anchors.centerIn: parent
                width: parent.width - 40
                spacing: 8
                visible: entriesView.count === 0

                BusyIndicator {
                    anchors.horizontalCenter: parent.horizontalCenter
                    running: fileManagerController.busy
                    visible: running
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: "#666666"
                    text: fileManagerController.busy
                          ? "Loading files from the connected device..."
                          : "No entries in this folder."
                }
            }
        }

        ProgressBar {
            Layout.fillWidth: true
            from: 0
            to: 1
            value: fileManagerController.transferProgress
            visible: fileManagerController.busy || fileManagerController.transferProgress > 0
        }

        Text {
            Layout.fillWidth: true
            text: fileManagerController.statusMessage
            wrapMode: Text.WordWrap
            color: "#444444"
            font.pixelSize: 15
        }
    }
}
