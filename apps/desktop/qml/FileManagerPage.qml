import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import LibreConnect.desktop 1.0

Page {
    id: root

    property var windowRef: null
    property bool standaloneWindow: false
    property var selectedRemotePaths: []
    property int lastInteractedIndex: -1
    property var pathSegments: []
    property bool dropActive: false
    property bool pathEditMode: false
    readonly property int selectedCount: selectedRemotePaths.length
    readonly property string windowTitleSuffix: "File Manager"
    signal requestClose()

    FileManagerController {
        id: fileManagerController
        onCurrentRemotePathChanged: root.rebuildPathSegments()
        onRemoteEntriesChanged: root.pruneSelectionToVisibleEntries()
    }

    function rebuildPathSegments() {
        const currentPath = fileManagerController.currentRemotePath
        const segments = []
        const normalized = currentPath && currentPath.length > 0 ? currentPath : "/"

        if (normalized === "/") {
            segments.push({ "label": "/", "path": "/" })
            pathSegments = segments
            return
        }

        const parts = normalized.split("/")
        let runningPath = ""

        if (normalized.charAt(0) === "/") {
            segments.push({ "label": "/", "path": "/" })
            runningPath = ""
        }

        for (let i = 0; i < parts.length; ++i) {
            const part = parts[i]
            if (!part || part.length === 0)
                continue

            runningPath += "/" + part
            segments.push({ "label": part, "path": runningPath })
        }

        pathSegments = segments
    }

    function isPathSelected(path) {
        return selectedRemotePaths.indexOf(path) >= 0
    }

    function clearSelection() {
        selectedRemotePaths = []
        lastInteractedIndex = -1
    }

    function setSingleSelection(path, index) {
        selectedRemotePaths = path && path.length > 0 ? [path] : []
        lastInteractedIndex = index
    }

    function toggleSelection(path, index) {
        if (!path || path.length === 0)
            return

        const next = selectedRemotePaths.slice()
        const existingIndex = next.indexOf(path)
        if (existingIndex >= 0)
            next.splice(existingIndex, 1)
        else
            next.push(path)

        selectedRemotePaths = next
        lastInteractedIndex = index
    }

    function selectRange(index) {
        if (index < 0 || index >= fileManagerController.remoteEntries.length)
            return

        if (lastInteractedIndex < 0 || lastInteractedIndex >= fileManagerController.remoteEntries.length) {
            const item = fileManagerController.remoteEntries[index]
            setSingleSelection(item.path, index)
            return
        }

        const first = Math.min(lastInteractedIndex, index)
        const last = Math.max(lastInteractedIndex, index)
        const next = []

        for (let i = first; i <= last; ++i) {
            const entry = fileManagerController.remoteEntries[i]
            if (entry && entry.path)
                next.push(entry.path)
        }

        selectedRemotePaths = next
    }

    function selectionForContext(targetPath, index) {
        if (isPathSelected(targetPath))
            return

        setSingleSelection(targetPath, index)
    }

    function pruneSelectionToVisibleEntries() {
        const visible = {}
        for (let i = 0; i < fileManagerController.remoteEntries.length; ++i) {
            const entry = fileManagerController.remoteEntries[i]
            visible[entry.path] = true
        }

        const filtered = []
        for (let i = 0; i < selectedRemotePaths.length; ++i) {
            const path = selectedRemotePaths[i]
            if (visible[path])
                filtered.push(path)
        }

        if (filtered.length !== selectedRemotePaths.length)
            selectedRemotePaths = filtered

        if (selectedRemotePaths.length === 0)
            lastInteractedIndex = -1
    }

    function entryByPath(path) {
        for (let i = 0; i < fileManagerController.remoteEntries.length; ++i) {
            const entry = fileManagerController.remoteEntries[i]
            if (entry.path === path)
                return entry
        }

        return null
    }

    function uploadLocalUrls(urls) {
        if (!urls || urls.length === 0)
            return

        for (let i = 0; i < urls.length; ++i)
            fileManagerController.uploadLocalEntry(urls[i])
    }

    function beginPathEdit() {
        pathEditMode = true
        pathEditField.text = fileManagerController.currentRemotePath
        pathEditField.forceActiveFocus()
        pathEditField.selectAll()
    }

    function commitPathEdit() {
        const nextPath = pathEditField.text
        pathEditMode = false
        fileManagerController.browseTo(nextPath)
    }

    function cancelPathEdit() {
        pathEditMode = false
        pathEditField.text = fileManagerController.currentRemotePath
    }

    background: Rectangle {
        color: Theme.backgroundColor
    }

    FolderDialog {
        id: folderDialog
        title: "Choose download folder"
        onAccepted: {
            if (selectedFolder)
                fileManagerController.setLocalDownloadDirectory(selectedFolder)
        }
    }

    FileDialog {
        id: uploadDialog
        title: "Select files to upload"
        fileMode: FileDialog.OpenFiles
        onAccepted: root.uploadLocalUrls(selectedFiles)
    }

    FolderDialog {
        id: uploadFolderDialog
        title: "Select folder to upload"
        onAccepted: {
            if (selectedFolder)
                fileManagerController.uploadLocalEntry(selectedFolder)
        }
    }

    Menu {
        id: fileContextMenu
        property var targetPaths: []
        property string targetPath: ""
        property bool singleTargetIsDirectory: false

        MenuItem {
            text: fileContextMenu.singleTargetIsDirectory ? "Open Folder" : "Open"
            enabled: fileContextMenu.targetPaths.length === 1
            onTriggered: {
                if (fileContextMenu.singleTargetIsDirectory)
                    fileManagerController.browseTo(fileContextMenu.targetPath)
                else
                    fileManagerController.openEntry(fileContextMenu.targetPath)
            }
        }

        MenuItem {
            text: "Copy"
            enabled: fileContextMenu.targetPaths.length > 0
            onTriggered: fileManagerController.copyEntries(fileContextMenu.targetPaths)
        }

        MenuItem {
            text: "Download"
            enabled: fileContextMenu.targetPaths.length > 0
            onTriggered: fileManagerController.downloadEntries(fileContextMenu.targetPaths)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 20

        Row {
            Layout.fillWidth: true
            spacing: 16

            ThemedButton {
                text: standaloneWindow ? "Close" : "Back"
                width: 100
                height: 42
                onClicked: {
                    if (standaloneWindow) {
                        requestClose()
                    } else if (windowRef) {
                        windowRef.goBack()
                    }
                }
            }

            Text {
                text: "File Manager"
                font.family: Theme.fontFamily
                font.pixelSize: 30
                font.bold: true
                color: Theme.textColor
                verticalAlignment: Text.AlignVCenter
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: controlsColumn.implicitHeight + 36
            radius: 12
            color: Theme.panelColor
            border.color: Theme.panelBorderColor

            Column {
                id: controlsColumn
                anchors.fill: parent
                anchors.margins: 18
                spacing: 14

                Row {
                    width: parent.width
                    spacing: 10

                    Rectangle {
                        width: parent.width - 230
                        height: 36
                        radius: 6
                        color: Theme.backgroundColor
                        border.color: Theme.panelBorderColor
                        border.width: 1

                        TextField {
                            id: pathEditField
                            anchors.fill: parent
                            anchors.margins: 4
                            visible: root.pathEditMode
                            color: Theme.textColor
                            placeholderText: "Remote path"
                            placeholderTextColor: Theme.subtleTextColor
                            selectedTextColor: Theme.textColor
                            selectionColor: Theme.selectedColor
                            background: null
                            onAccepted: root.commitPathEdit()
                            onEditingFinished: {
                                if (!focus && root.pathEditMode)
                                    root.commitPathEdit()
                            }
                            Keys.onEscapePressed: root.cancelPathEdit()
                        }

                        Row {
                            anchors.fill: parent
                            anchors.margins: 3
                            spacing: 8
                            visible: !root.pathEditMode

                            Flickable {
                                id: breadcrumbFlickable
                                height: parent.height
                                width: Math.min(contentWidth, parent.width - 28 - parent.spacing)
                                contentWidth: breadcrumbRow.width
                                contentHeight: breadcrumbRow.height
                                clip: true
                                ScrollBar.horizontal: ScrollBar {
                                    policy: ScrollBar.AsNeeded
                                }

                                Row {
                                    id: breadcrumbRow
                                    spacing: 6

                                    Repeater {
                                        model: root.pathSegments

                                        delegate: Row {
                                            spacing: 6

                                            ThemedButton {
                                                text: modelData.label
                                                height: 30
                                                width: Math.max(46, contentItem.implicitWidth + 16)
                                                onClicked: fileManagerController.browseTo(modelData.path)
                                            }

                                            Text {
                                                visible: index < root.pathSegments.length - 1
                                                text: ">"
                                                font.family: Theme.fontFamily
                                                color: Theme.subtleTextColor
                                                verticalAlignment: Text.AlignVCenter
                                            }
                                        }
                                    }
                                }
                            }

                            Item {
                                id: editArea
                                height: parent.height
                                width: Math.max(28, parent.width - breadcrumbFlickable.width - parent.spacing)

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: root.beginPathEdit()
                                }
                            }
                        }
                    }

                    ThemedButton {
                        text: "Up"
                        width: 70
                        onClicked: fileManagerController.goUp()
                    }

                    ThemedButton {
                        text: "Refresh"
                        width: 120
                        onClicked: fileManagerController.refreshEntries()
                    }
                }

                Row {
                    width: parent.width
                    spacing: 12

                    ThemedButton {
                        text: "Choose Download Folder"
                        width: 220
                        onClicked: folderDialog.open()
                    }

                    Text {
                        text: fileManagerController.localDownloadDirectory
                        width: parent.width - 240
                        wrapMode: Text.WordWrap
                        color: Theme.mutedTextColor
                        font.family: Theme.fontFamily
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Row {
                    width: parent.width
                    spacing: 10

                    ThemedButton {
                        text: "Copy"
                        width: 110
                        enabled: root.selectedCount > 0
                        onClicked: fileManagerController.copyEntries(root.selectedRemotePaths)
                    }

                    ThemedButton {
                        text: "Download"
                        width: 130
                        enabled: root.selectedCount > 0
                        onClicked: fileManagerController.downloadEntries(root.selectedRemotePaths)
                    }

                    ThemedButton {
                        text: "Clear"
                        width: 80
                        enabled: root.selectedCount > 0
                        onClicked: root.clearSelection()
                    }

                    Item {
                        width: Math.max(0, parent.width - (110 + 130 + 80 + 150 + 160 + (5 * parent.spacing)))
                        height: 1
                    }

                    ThemedButton {
                        text: "Upload Files"
                        width: 150
                        onClicked: uploadDialog.open()
                    }

                    ThemedButton {
                        text: "Upload Folder"
                        width: 160
                        onClicked: uploadFolderDialog.open()
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 180
            radius: 12
            color: root.dropActive ? Qt.lighter(Theme.panelColor, 1.08) : Theme.panelColor
            border.width: root.dropActive ? 2 : 1
            border.color: root.dropActive ? Theme.selectedBorderColor : Theme.panelBorderColor

            ListView {
                id: entriesView
                anchors.fill: parent
                anchors.margins: 10
                clip: true
                spacing: 8
                model: fileManagerController.remoteEntries
                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }

                delegate: Rectangle {
                    required property int index
                    required property var modelData

                    width: entriesView.width
                    height: 58
                    radius: 10
                    color: root.isPathSelected(modelData.path)
                           ? Theme.selectedColor
                           : (modelData.isDirectory ? Qt.lighter(Theme.panelColor, 1.05) : Theme.panelColor)
                    border.color: root.isPathSelected(modelData.path)
                                  ? Theme.selectedBorderColor
                                  : Theme.panelBorderColor

                    Row {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 12

                        Image {
                            source: Theme.dark && modelData.iconSource.endsWith(".svg")
                                    ? modelData.iconSource.replace(".svg", "_dark.svg")
                                    : modelData.iconSource
                            width: 24
                            height: 24
                            anchors.verticalCenter: parent.verticalCenter
                            fillMode: Image.PreserveAspectFit
                        }

                        Column {
                            width: parent.width - 50
                            spacing: 2

                            Text {
                                text: modelData.name
                                font.family: Theme.fontFamily
                                font.pixelSize: 16
                                font.bold: true
                                color: Theme.textColor
                                elide: Text.ElideRight
                                width: parent.width
                            }

                            Text {
                                text: modelData.typeLabel + "  |  " + modelData.sizeLabel
                                font.family: Theme.fontFamily
                                font.pixelSize: 12
                                color: Theme.subtleTextColor
                                elide: Text.ElideRight
                                width: parent.width
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton

                        onPressed: function(mouse) {
                            if (mouse.button === Qt.RightButton) {
                                root.selectionForContext(modelData.path, index)
                                fileContextMenu.targetPaths = root.selectedRemotePaths.slice()
                                fileContextMenu.targetPath = modelData.path
                                const targetEntry = root.entryByPath(modelData.path)
                                fileContextMenu.singleTargetIsDirectory = targetEntry && targetEntry.isDirectory
                                const position = mapToItem(root, mouse.x, mouse.y)
                                fileContextMenu.popup(position.x, position.y)
                                return
                            }

                            if (mouse.modifiers & Qt.ShiftModifier) {
                                root.selectRange(index)
                            } else if (mouse.modifiers & Qt.ControlModifier) {
                                root.toggleSelection(modelData.path, index)
                            } else {
                                root.setSingleSelection(modelData.path, index)
                            }
                        }

                        onDoubleClicked: {
                            root.setSingleSelection(modelData.path, index)
                            if (modelData.isDirectory) {
                                fileManagerController.browseTo(modelData.path)
                            } else {
                                fileManagerController.openEntry(modelData.path)
                            }
                        }
                    }
                }
            }

            DropArea {
                anchors.fill: parent
                onEntered: function(drag) {
                    if (!drag.hasUrls)
                        return

                    root.dropActive = true
                    drag.acceptProposedAction()
                }
                onExited: root.dropActive = false
                onDropped: function(drop) {
                    root.dropActive = false
                    if (!drop.hasUrls || drop.urls.length === 0)
                        return

                    root.uploadLocalUrls(drop.urls)
                    drop.acceptProposedAction()
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
                    color: Theme.subtleTextColor
                    font.family: Theme.fontFamily
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

        RowLayout {
            Layout.fillWidth: true
            spacing: 14

            Text {
                Layout.fillWidth: true
                text: fileManagerController.statusMessage
                wrapMode: Text.WordWrap
                color: Theme.mutedTextColor
                font.family: Theme.fontFamily
                font.pixelSize: 15
            }

            Text {
                text: root.selectedCount > 0
                      ? ("Files selected: " + root.selectedCount)
                      : "Files selected: 0"
                color: Theme.mutedTextColor
                font.family: Theme.fontFamily
                font.pixelSize: 15
                horizontalAlignment: Text.AlignRight
            }
        }
    }

    Component.onCompleted: rebuildPathSegments()
}
