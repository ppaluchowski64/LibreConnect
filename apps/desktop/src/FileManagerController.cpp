#include "FileManagerController.h"

#include <QPointer>
#include <QStandardPaths>

#include <algorithm>
#include <filesystem>

#include <DebugLog.h>
#include <ConnectionManager.h>
#include <FileShareEvents.h>
#include <FileShareModule.h>
#include <FileEntry.h>
#include <ModulesManager.h>

namespace {
QString fileTypeLabel(const std::optional<FileType>& type)
{
    if (!type.has_value()) {
        return QStringLiteral("Unknown");
    }

    switch (type.value()) {
    case FileType::Directory:
        return QStringLiteral("Folder");
    case FileType::Text:
        return QStringLiteral("Text");
    case FileType::Image:
        return QStringLiteral("Image");
    case FileType::Video:
        return QStringLiteral("Video");
    case FileType::Audio:
        return QStringLiteral("Audio");
    case FileType::Document:
        return QStringLiteral("Document");
    case FileType::Archive:
        return QStringLiteral("Archive");
    case FileType::Executable:
        return QStringLiteral("Executable");
    case FileType::Unknown:
    default:
        return QStringLiteral("Unknown");
    }
}

QString fileTypeIconSource(const std::optional<FileType>& type)
{
    if (!type.has_value()) {
        return QStringLiteral("qrc:/LibreConnect/desktop/unknown.svg");
    }

    switch (type.value()) {
    case FileType::Directory:
        return QStringLiteral("qrc:/LibreConnect/desktop/folder.svg");
    case FileType::Text:
        return QStringLiteral("qrc:/LibreConnect/desktop/text.svg");
    case FileType::Image:
        return QStringLiteral("qrc:/LibreConnect/desktop/image.svg");
    case FileType::Video:
        return QStringLiteral("qrc:/LibreConnect/desktop/video.svg");
    case FileType::Audio:
        return QStringLiteral("qrc:/LibreConnect/desktop/audio.svg");
    case FileType::Document:
        return QStringLiteral("qrc:/LibreConnect/desktop/document.svg");
    case FileType::Archive:
        return QStringLiteral("qrc:/LibreConnect/desktop/archive.svg");
    case FileType::Executable:
        return QStringLiteral("qrc:/LibreConnect/desktop/executable.svg");
    case FileType::Unknown:
    default:
        return QStringLiteral("qrc:/LibreConnect/desktop/unknown.svg");
    }
}

QString formatEntrySize(const FileEntry& entry)
{
    const std::optional<FileType> type = entry.GetType();
    if (type.has_value() && type.value() == FileType::Directory) {
        return QStringLiteral("--");
    }

    const std::optional<size_t> size = entry.GetSize();
    if (!size.has_value()) {
        return QStringLiteral("Unknown");
    }

    static const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    constexpr int unitCount = static_cast<int>(sizeof(units) / sizeof(units[0]));

    double value = static_cast<double>(size.value());
    int unitIndex = 0;
    while (value >= 1024.0 && unitIndex < unitCount - 1) {
        value /= 1024.0;
        ++unitIndex;
    }

    const int precision = unitIndex == 0 ? 0 : 1;
    return QStringLiteral("%1 %2").arg(QString::number(value, 'f', precision), QString::fromLatin1(units[unitIndex]));
}
}

FileManagerController::FileManagerController(QObject* parent)
    : QObject(parent)
{
    ConnectionManager::AddEventListener(QPointer<QObject>(this));

    const QString defaultDownloadDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    m_localDownloadDirectory = defaultDownloadDir.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
        : defaultDownloadDir;

    m_pollTimer.setInterval(250);
    connect(&m_pollTimer, &QTimer::timeout, this, &FileManagerController::refreshModuleState);
    m_pollTimer.start();

    QTimer::singleShot(0, this, &FileManagerController::refreshEntries);
}

void FileManagerController::setLocalDownloadDirectory(const QUrl& directoryUrl)
{
    const QString localPath = directoryUrl.toLocalFile();
    if (localPath.isEmpty() || m_localDownloadDirectory == localPath) {
        return;
    }

    m_localDownloadDirectory = localPath;
    emit localDownloadDirectoryChanged();
}

void FileManagerController::refreshEntries()
{
    loadDirectory(m_currentRemotePath);
}

void FileManagerController::browseTo(const QString& remotePath)
{
    const QString normalizedPath = normalizeRemotePath(remotePath);
    if (normalizedPath.isEmpty()) {
        return;
    }

    loadDirectory(normalizedPath);
}

void FileManagerController::goUp()
{
    const QString parentPath = parentRemotePath(m_currentRemotePath);
    if (parentPath == m_currentRemotePath) {
        return;
    }

    loadDirectory(parentPath);
}

void FileManagerController::downloadEntry(const QString& remotePath)
{
    downloadEntries(QStringList{remotePath});
}

void FileManagerController::downloadEntries(const QStringList& remotePaths)
{
    if (remotePaths.isEmpty()) {
        setStatusMessage(QStringLiteral("Select one or more files or folders first."));
        return;
    }

    if (m_localDownloadDirectory.isEmpty()) {
        setStatusMessage(QStringLiteral("Choose a local download folder first."));
        return;
    }

    QStringList queue;
    queue.reserve(remotePaths.size());
    for (const QString& remotePath : remotePaths) {
        const QString normalizedPath = normalizeRemotePath(remotePath);
        if (normalizedPath.isEmpty()) {
            continue;
        }

        auto lookup = m_entryLookup.find(normalizedPath.toStdString());
        if (lookup == m_entryLookup.end()) {
            continue;
        }

        if (!queue.contains(normalizedPath)) {
            queue.push_back(normalizedPath);
        }
    }

    if (queue.isEmpty()) {
        setStatusMessage(QStringLiteral("The selected entries are no longer available in the current folder."));
        return;
    }

    const bool actionInProgress = m_pendingAction != PendingAction::None || m_waitingForModule;
    if (actionInProgress) {
        int queuedCount = 0;
        for (const QString& path : queue) {
            if (path == m_pendingEntryPath || path == m_activeEntryPath || m_pendingDownloadQueue.contains(path)) {
                continue;
            }

            m_pendingDownloadQueue.push_back(path);
            ++queuedCount;
        }

        if (queuedCount == 0) {
            setStatusMessage(QStringLiteral("Those entries are already queued or currently downloading."));
            return;
        }

        if (!m_downloadBatchActive) {
            m_downloadBatchActive = true;
            m_downloadBatchTotal = m_pendingAction == PendingAction::Download ? (1 + queuedCount) : queuedCount;
            m_downloadBatchCompleted = 0;
            m_downloadBatchFailed = 0;
        } else {
            m_downloadBatchTotal += queuedCount;
        }

        setStatusMessage(queuedCount == 1
            ? QStringLiteral("Queued 1 entry for download.")
            : QStringLiteral("Queued %1 entries for download.").arg(queuedCount));
        return;
    }

    if (queue.size() == 1) {
        m_pendingDownloadQueue.clear();
        m_downloadBatchActive = false;
        m_downloadBatchTotal = 1;
        m_downloadBatchCompleted = 0;
        m_downloadBatchFailed = 0;
        beginDownloadForPath(queue.front(), false);
        return;
    }

    m_pendingDownloadQueue = queue;
    m_downloadBatchActive = true;
    m_downloadBatchTotal = m_pendingDownloadQueue.size();
    m_downloadBatchCompleted = 0;
    m_downloadBatchFailed = 0;
    startNextQueuedDownload();
}

void FileManagerController::openEntry(const QString& remotePath)
{
    const QString normalizedPath = normalizeRemotePath(remotePath);
    if (normalizedPath.isEmpty()) {
        setStatusMessage(QStringLiteral("Select a file first."));
        return;
    }

    auto lookup = m_entryLookup.find(normalizedPath.toStdString());
    if (lookup == m_entryLookup.end()) {
        setStatusMessage(QStringLiteral("That entry is no longer available in the current folder."));
        return;
    }

    if (lookup->second.GetType().has_value() && lookup->second.GetType().value() == FileType::Directory) {
        setStatusMessage(QStringLiteral("Folders cannot be opened directly."));
        return;
    }

    if (m_pendingAction != PendingAction::None || m_waitingForModule) {
        if (normalizedPath == m_pendingEntryPath
            || normalizedPath == m_activeEntryPath
            || m_pendingOpenQueue.contains(normalizedPath)) {
            setStatusMessage(QStringLiteral("That entry is already queued or currently being opened."));
            return;
        }

        m_pendingOpenQueue.push_back(normalizedPath);
        const QString entryName = QString::fromStdString(lookup->second.GetName().value_or(std::string()));
        setStatusMessage(QStringLiteral("Queued %1 to open.").arg(entryName.isEmpty() ? normalizedPath : entryName));
        return;
    }

    m_pendingEntryPath = normalizedPath;
    m_pendingLocalPath.clear();
    m_activeEntryPath = normalizedPath;
    m_activeEntryName = QString::fromStdString(lookup->second.GetName().value_or(std::string()));
    m_pendingAction = PendingAction::Open;
    setTransferProgress(0.0);

    auto& module = ModulesManager::GetModuleReference<FileShareModule>();
    if (module->GetModuleState() != ModuleState::Enabled) {
        m_waitingForModule = true;
        setBusy(true);
        setStatusMessage(QStringLiteral("Preparing file channels..."));
        module->Enable(true);
        return;
    }

    startPendingActionIfReady();
}

void FileManagerController::copyEntry(const QString& remotePath)
{
    copyEntries(QStringList{remotePath});
}

void FileManagerController::copyEntries(const QStringList& remotePaths)
{
    if (remotePaths.isEmpty()) {
        setStatusMessage(QStringLiteral("Select one or more files or folders first."));
        return;
    }

    std::vector<FileEntry> entries;
    entries.reserve(static_cast<size_t>(remotePaths.size()));

    QStringList uniquePaths;
    for (const QString& remotePath : remotePaths) {
        const QString normalizedPath = normalizeRemotePath(remotePath);
        if (normalizedPath.isEmpty() || uniquePaths.contains(normalizedPath)) {
            continue;
        }

        auto lookup = m_entryLookup.find(normalizedPath.toStdString());
        if (lookup == m_entryLookup.end()) {
            continue;
        }

        uniquePaths.push_back(normalizedPath);
        entries.push_back(lookup->second);
    }

    if (entries.empty()) {
        setStatusMessage(QStringLiteral("The selected entries are no longer available in the current folder."));
        return;
    }

    if (m_pendingAction != PendingAction::None || m_waitingForModule) {
        bool duplicateQueueRequest = m_pendingAction == PendingAction::Copy && m_pendingCopyPaths == uniquePaths;
        if (!duplicateQueueRequest) {
            for (const QStringList& queuedPaths : m_pendingCopyQueue) {
                if (queuedPaths == uniquePaths) {
                    duplicateQueueRequest = true;
                    break;
                }
            }
        }

        if (duplicateQueueRequest) {
            setStatusMessage(QStringLiteral("Those entries are already queued for copy."));
            return;
        }

        m_pendingCopyQueue.push_back(uniquePaths);
        setStatusMessage(uniquePaths.size() == 1
            ? QStringLiteral("Queued 1 entry for copy.")
            : QStringLiteral("Queued %1 entries for copy.").arg(uniquePaths.size()));
        return;
    }

    m_pendingCopyPaths = uniquePaths;
    m_pendingEntryPath = uniquePaths.size() == 1 ? uniquePaths.front() : QString();
    m_pendingLocalPath.clear();
    m_activeEntryPath = m_pendingEntryPath;
    m_activeEntryName = uniquePaths.size() == 1
        ? QString::fromStdString(entries.front().GetName().value_or(std::string()))
        : QStringLiteral("%1 entries").arg(entries.size());
    m_pendingAction = PendingAction::Copy;
    setTransferProgress(0.0);

    auto& module = ModulesManager::GetModuleReference<FileShareModule>();
    if (module->GetModuleState() != ModuleState::Enabled) {
        m_waitingForModule = true;
        setBusy(true);
        setStatusMessage(QStringLiteral("Preparing clipboard transfer..."));
        module->Enable(true);
        return;
    }

    m_waitingForModule = false;
    setBusy(true);
    setStatusMessage(uniquePaths.size() == 1
        ? QStringLiteral("Copying %1 to clipboard...").arg(m_activeEntryName)
        : QStringLiteral("Copying %1 entries to clipboard...").arg(uniquePaths.size()));
    module->CopyEntriesToClipboard(std::move(entries));
}

void FileManagerController::uploadLocalEntry(const QUrl& localPathUrl)
{
    const QString localPath = localPathUrl.toLocalFile().trimmed();
    if (localPath.isEmpty()) {
        setStatusMessage(QStringLiteral("Choose a local file or folder first."));
        return;
    }

    const std::filesystem::path sourcePath = localPath.toStdString();
    if (!std::filesystem::exists(sourcePath)) {
        setStatusMessage(QStringLiteral("The selected local file or folder does not exist anymore."));
        return;
    }

    if (m_pendingUploadQueue.contains(localPath) || m_pendingLocalPath == localPath) {
        return;
    }

    if (!m_uploadBatchActive) {
        m_uploadBatchActive = true;
        m_uploadBatchTotal = 0;
        m_uploadBatchCompleted = 0;
        m_uploadBatchFailed = 0;
    }

    m_pendingUploadQueue.push_back(localPath);
    ++m_uploadBatchTotal;

    if (m_pendingAction != PendingAction::None || m_waitingForModule) {
        setStatusMessage(QStringLiteral("Queued %1 for upload.").arg(QString::fromStdString(sourcePath.filename().string())));
        return;
    }

    startNextQueuedUpload();
}

bool FileManagerController::event(QEvent* event)
{
    if (event->type() == ModuleErrorEvent::Type) {
        auto* moduleErrorEvent = static_cast<ModuleErrorEvent*>(event);
        const QString message = QStringLiteral("%1 module error: %2.")
            .arg(QString::fromLatin1(ModuleTypeToString(moduleErrorEvent->GetModuleType())))
            .arg(QString::fromLatin1(ModuleFailReasonToString(moduleErrorEvent->GetError())));

        if (moduleErrorEvent->GetModuleType() == ModuleType::NetworkFileSystem) {
            setBusy(false);
            setStatusMessage(message);
        } else {
            Debug::LogWarning("FileManagerController received unrelated ModuleErrorEvent: {}", message.toStdString());
        }

        return true;
    }

    if (event->type() == FetchDirectoryEntriesResultEvent::Type) {
        auto* directoryEvent = static_cast<FetchDirectoryEntriesResultEvent*>(event);
        const QString eventPath = normalizeRemotePath(QString::fromStdString(directoryEvent->GetPath()));
        if (eventPath != m_currentRemotePath) {
            return true;
        }

        std::vector<FileEntry> entries = directoryEvent->TakeEntries();
        std::sort(entries.begin(), entries.end(), [](const FileEntry& lhs, const FileEntry& rhs) {
            const bool lhsIsDir = lhs.GetType().has_value() && lhs.GetType().value() == FileType::Directory;
            const bool rhsIsDir = rhs.GetType().has_value() && rhs.GetType().value() == FileType::Directory;
            if (lhsIsDir != rhsIsDir) {
                return lhsIsDir;
            }

            return lhs.GetName().value_or(std::string()) < rhs.GetName().value_or(std::string());
        });

        QVariantList remoteEntries;
        remoteEntries.reserve(static_cast<qsizetype>(entries.size()));
        m_entryLookup.clear();

        for (const FileEntry& entry : entries) {
            const QString fullPath = composeRemotePath(entry);
            if (fullPath.isEmpty()) {
                continue;
            }

            m_entryLookup.insert_or_assign(fullPath.toStdString(), entry);
            remoteEntries.push_back(toVariantMap(entry));
        }

        setRemoteEntries(remoteEntries);
        setBusy(false);
        setStatusMessage(remoteEntries.isEmpty()
            ? QStringLiteral("This folder is empty.")
            : QStringLiteral("Loaded %1 entries.").arg(remoteEntries.size()));
        startNextQueuedAction();
        return true;
    }

    if (event->type() == EntryTransferProgressEvent::Type) {
        if (m_pendingAction == PendingAction::None) {
            return true;
        }

        auto* progressEvent = static_cast<EntryTransferProgressEvent*>(event);
        const TransferOperation operation = progressEvent->GetOperation();
        const QString entryPath = composeRemotePath(progressEvent->GetFileEntry());

        if (!m_activeEntryPath.isEmpty() && entryPath != m_activeEntryPath)
            return true;

        const size_t totalBytes = progressEvent->GetTotalBytes();
        const size_t transferredBytes = progressEvent->GetBytesTransferred();
        const double progress = totalBytes == 0 ? 0.0 : static_cast<double>(transferredBytes) / static_cast<double>(totalBytes);
        setTransferProgress(progress);
        setBusy(true);
        setStatusMessage(QStringLiteral("%1 %2 (%3%)").arg(
            operation == TransferOperation::Post ? QStringLiteral("Uploading") : QStringLiteral("Transferring"),
            m_activeEntryName,
            QString::number(progress * 100.0, 'f', 0)));
        return true;
    }

    if (event->type() == EntryTransferResultEvent::Type) {
        if (m_pendingAction == PendingAction::None) {
            return true;
        }

        const PendingAction completedAction = m_pendingAction;
        auto* resultEvent = static_cast<EntryTransferResultEvent*>(event);
        const QString entryPath = composeRemotePath(resultEvent->GetFileEntry());
        if (!m_activeEntryPath.isEmpty() && entryPath != m_activeEntryPath) {
            return true;
        }

        if (m_pendingAction == PendingAction::Copy) {
            return true;
        }

        setTransferProgress(resultEvent->Success() ? 1.0 : 0.0);
        if (m_pendingAction == PendingAction::Upload) {
            ++m_uploadBatchCompleted;
            if (!resultEvent->Success()) {
                ++m_uploadBatchFailed;
            }

            if (resultEvent->Success()) {
                refreshEntries();
            }

            if (!m_pendingUploadQueue.isEmpty()) {
                startNextQueuedUpload();
                return true;
            }

            setBusy(false);
            if (m_uploadBatchTotal <= 1) {
                setStatusMessage(resultEvent->Success()
                    ? QStringLiteral("Uploaded %1 to %2.").arg(m_activeEntryName, m_currentRemotePath)
                    : QStringLiteral("Failed to upload %1.").arg(m_activeEntryName));
            } else {
                setStatusMessage(m_uploadBatchFailed == 0
                    ? QStringLiteral("Uploaded %1 files to %2.").arg(m_uploadBatchCompleted).arg(m_currentRemotePath)
                    : QStringLiteral("Uploaded %1 of %2 files (%3 failed).")
                        .arg(m_uploadBatchCompleted - m_uploadBatchFailed)
                        .arg(m_uploadBatchTotal)
                        .arg(m_uploadBatchFailed));
            }
        } else if (m_pendingAction == PendingAction::Open) {
            setBusy(false);
            setStatusMessage(resultEvent->Success()
                ? QStringLiteral("Opened %1.").arg(m_activeEntryName)
                : QStringLiteral("Failed to open %1.").arg(m_activeEntryName));
        } else {
            if (m_downloadBatchActive) {
                ++m_downloadBatchCompleted;
                if (!resultEvent->Success()) {
                    ++m_downloadBatchFailed;
                }

                if (!m_pendingDownloadQueue.isEmpty()) {
                    startNextQueuedDownload();
                    return true;
                }

                setBusy(false);
                setStatusMessage(m_downloadBatchFailed == 0
                    ? QStringLiteral("Downloaded %1 entries to %2.").arg(m_downloadBatchCompleted).arg(m_localDownloadDirectory)
                    : QStringLiteral("Downloaded %1 of %2 entries (%3 failed).")
                        .arg(m_downloadBatchCompleted - m_downloadBatchFailed)
                        .arg(m_downloadBatchTotal)
                        .arg(m_downloadBatchFailed));
            } else {
                setBusy(false);
                setStatusMessage(resultEvent->Success()
                    ? QStringLiteral("Downloaded %1 to %2.").arg(m_activeEntryName, m_localDownloadDirectory)
                    : QStringLiteral("Failed to download %1.").arg(m_activeEntryName));
            }
        }

        m_pendingEntryPath.clear();
        m_pendingLocalPath.clear();
        m_activeEntryPath.clear();
        m_activeEntryName.clear();
        m_pendingCopyPaths.clear();

        if (completedAction == PendingAction::Download) {
            m_pendingDownloadQueue.clear();
            m_downloadBatchActive = false;
            m_downloadBatchTotal = 0;
            m_downloadBatchCompleted = 0;
            m_downloadBatchFailed = 0;
        } else if (completedAction == PendingAction::Upload) {
            m_pendingUploadQueue.clear();
            m_uploadBatchActive = false;
            m_uploadBatchTotal = 0;
            m_uploadBatchCompleted = 0;
            m_uploadBatchFailed = 0;
        }

        m_waitingForModule = false;
        m_pendingAction = PendingAction::None;
        startNextQueuedAction();
        return true;
    }

    if (event->type() == EntriesCopyResultEvent::Type) {
        auto* resultEvent = static_cast<EntriesCopyResultEvent*>(event);
        if (m_pendingAction != PendingAction::Copy) {
            return true;
        }

        setBusy(false);
        setTransferProgress(resultEvent->Success() ? 1.0 : 0.0);
        setStatusMessage(resultEvent->Success()
            ? QStringLiteral("Copied %1 to clipboard.").arg(m_activeEntryName)
            : QStringLiteral("Failed to copy %1 to clipboard.").arg(m_activeEntryName));

        m_pendingEntryPath.clear();
        m_pendingLocalPath.clear();
        m_activeEntryPath.clear();
        m_activeEntryName.clear();
        m_pendingCopyPaths.clear();
        m_waitingForModule = false;
        m_pendingAction = PendingAction::None;
        startNextQueuedAction();
        return true;
    }

    return QObject::event(event);
}

void FileManagerController::refreshModuleState()
{
    if (!m_waitingForModule) {
        return;
    }

    startPendingActionIfReady();
}

void FileManagerController::startNextQueuedAction()
{
    if (m_pendingAction != PendingAction::None || m_waitingForModule) {
        return;
    }

    if (!m_pendingDownloadQueue.isEmpty()) {
        if (!m_downloadBatchActive) {
            m_downloadBatchActive = true;
            m_downloadBatchTotal = m_pendingDownloadQueue.size();
            m_downloadBatchCompleted = 0;
            m_downloadBatchFailed = 0;
        }

        startNextQueuedDownload();
        return;
    }

    if (!m_pendingOpenQueue.isEmpty()) {
        const QString nextPath = m_pendingOpenQueue.front();
        m_pendingOpenQueue.pop_front();
        openEntry(nextPath);
        return;
    }

    if (!m_pendingCopyQueue.isEmpty()) {
        const QStringList nextPaths = m_pendingCopyQueue.front();
        m_pendingCopyQueue.pop_front();
        copyEntries(nextPaths);
        return;
    }

    if (!m_pendingUploadQueue.isEmpty()) {
        if (!m_uploadBatchActive) {
            m_uploadBatchActive = true;
            if (m_uploadBatchTotal == 0) {
                m_uploadBatchTotal = m_pendingUploadQueue.size();
            }
        }
        startNextQueuedUpload();
    }
}

void FileManagerController::startPendingActionIfReady()
{
    if (m_pendingAction == PendingAction::None) {
        return;
    }

    auto& module = ModulesManager::GetModuleReference<FileShareModule>();
    if (module->GetModuleState() != ModuleState::Enabled) {
        return;
    }

    if (m_pendingAction == PendingAction::Browse) {
        const QString path = m_pendingBrowsePath.isEmpty() ? m_currentRemotePath : m_pendingBrowsePath;
        m_waitingForModule = false;
        m_pendingAction = PendingAction::None;
        m_pendingBrowsePath.clear();
        module->FetchDirectoryEntries(path.toStdString());
        return;
    }

    if (m_pendingAction == PendingAction::Upload) {
        if (m_pendingLocalPath.isEmpty()) {
            if (!m_pendingUploadQueue.isEmpty()) {
                startNextQueuedUpload();
                return;
            }

            m_waitingForModule = false;
            m_pendingAction = PendingAction::None;
            setBusy(false);
            setStatusMessage(QStringLiteral("No local file or folder selected for upload."));
            m_uploadBatchActive = false;
            m_uploadBatchTotal = 0;
            m_uploadBatchCompleted = 0;
            m_uploadBatchFailed = 0;
            startNextQueuedAction();
            return;
        }

        const std::filesystem::path localPath = m_pendingLocalPath.toStdString();
        if (!std::filesystem::exists(localPath)) {
            ++m_uploadBatchCompleted;
            ++m_uploadBatchFailed;
            if (!m_pendingUploadQueue.isEmpty()) {
                setStatusMessage(QStringLiteral("Skipped a file that no longer exists. Continuing queued uploads."));
                startNextQueuedUpload();
                return;
            }

            m_waitingForModule = false;
            m_pendingAction = PendingAction::None;
            setBusy(false);
            setStatusMessage(QStringLiteral("The selected local file or folder no longer exists."));
            m_uploadBatchActive = false;
            m_uploadBatchTotal = 0;
            m_uploadBatchCompleted = 0;
            m_uploadBatchFailed = 0;
            startNextQueuedAction();
            return;
        }

        m_waitingForModule = false;
        setBusy(true);
        setStatusMessage(QStringLiteral("Uploading %1...").arg(m_activeEntryName));
        module->PostEntry(localPath, m_currentRemotePath.toStdString());
        return;
    }

    if (m_pendingAction == PendingAction::Copy && !m_pendingCopyPaths.isEmpty()) {
        std::vector<FileEntry> entries;
        entries.reserve(static_cast<size_t>(m_pendingCopyPaths.size()));
        for (const QString& path : m_pendingCopyPaths) {
            auto lookup = m_entryLookup.find(path.toStdString());
            if (lookup != m_entryLookup.end()) {
                entries.push_back(lookup->second);
            }
        }

        if (entries.empty()) {
            m_waitingForModule = false;
            m_pendingAction = PendingAction::None;
            setBusy(false);
            setStatusMessage(QStringLiteral("The selected entries are no longer available."));
            startNextQueuedAction();
            return;
        }

        m_waitingForModule = false;
        setBusy(true);
        setStatusMessage(entries.size() == 1
            ? QStringLiteral("Copying %1 to clipboard...").arg(m_activeEntryName)
            : QStringLiteral("Copying %1 entries to clipboard...").arg(entries.size()));
        module->CopyEntriesToClipboard(std::move(entries));
        return;
    }

    if (m_pendingEntryPath.isEmpty()) {
        return;
    }

    auto lookup = m_entryLookup.find(m_pendingEntryPath.toStdString());
    if (lookup == m_entryLookup.end()) {
        m_waitingForModule = false;
        m_pendingAction = PendingAction::None;
        setBusy(false);
        setStatusMessage(QStringLiteral("The selected entry is no longer available."));
        startNextQueuedAction();
        return;
    }

    m_waitingForModule = false;
    setBusy(true);
    if (m_pendingAction == PendingAction::Open) {
        setStatusMessage(QStringLiteral("Opening %1...").arg(m_activeEntryName));
        module->OpenEntry(lookup->second);
    } else if (m_pendingAction == PendingAction::Copy) {
        setStatusMessage(QStringLiteral("Copying %1 to clipboard...").arg(m_activeEntryName));
        std::vector<FileEntry> entries;
        entries.push_back(lookup->second);
        module->CopyEntriesToClipboard(std::move(entries));
    } else {
        setStatusMessage(QStringLiteral("Downloading %1...").arg(m_activeEntryName));
        module->FetchEntry(lookup->second, m_localDownloadDirectory.toStdString());
    }
}

void FileManagerController::beginDownloadForPath(const QString& normalizedPath, const bool partOfBatch)
{
    auto lookup = m_entryLookup.find(normalizedPath.toStdString());
    if (lookup == m_entryLookup.end()) {
        if (partOfBatch) {
            ++m_downloadBatchCompleted;
            ++m_downloadBatchFailed;
            if (!m_pendingDownloadQueue.isEmpty()) {
                startNextQueuedDownload();
            } else {
                setBusy(false);
                setStatusMessage(QStringLiteral("Downloaded %1 of %2 entries (%3 failed).")
                    .arg(m_downloadBatchCompleted - m_downloadBatchFailed)
                    .arg(m_downloadBatchTotal)
                    .arg(m_downloadBatchFailed));
                m_pendingEntryPath.clear();
                m_pendingLocalPath.clear();
                m_activeEntryPath.clear();
                m_activeEntryName.clear();
                m_pendingDownloadQueue.clear();
                m_downloadBatchActive = false;
                m_downloadBatchTotal = 0;
                m_downloadBatchCompleted = 0;
                m_downloadBatchFailed = 0;
                m_waitingForModule = false;
                m_pendingAction = PendingAction::None;
                startNextQueuedAction();
            }
        } else {
            setStatusMessage(QStringLiteral("That entry is no longer available in the current folder."));
        }
        return;
    }

    m_pendingEntryPath = normalizedPath;
    m_pendingLocalPath.clear();
    m_activeEntryPath = normalizedPath;
    m_activeEntryName = QString::fromStdString(lookup->second.GetName().value_or(std::string()));
    m_pendingAction = PendingAction::Download;
    m_waitingForModule = false;
    setTransferProgress(0.0);

    auto& module = ModulesManager::GetModuleReference<FileShareModule>();
    if (module->GetModuleState() != ModuleState::Enabled) {
        m_waitingForModule = true;
        setBusy(true);
        setStatusMessage(QStringLiteral("Preparing download channels..."));
        module->Enable(true);
        return;
    }

    startPendingActionIfReady();
}

void FileManagerController::startNextQueuedDownload()
{
    if (m_pendingDownloadQueue.isEmpty()) {
        return;
    }

    const QString nextPath = m_pendingDownloadQueue.front();
    m_pendingDownloadQueue.pop_front();
    beginDownloadForPath(nextPath, true);
}

void FileManagerController::startNextQueuedUpload()
{
    if (m_pendingUploadQueue.isEmpty()) {
        return;
    }

    const QString nextPath = m_pendingUploadQueue.front();
    m_pendingUploadQueue.pop_front();
    beginUploadForLocalPath(nextPath);
}

void FileManagerController::beginUploadForLocalPath(const QString& localPath)
{
    const std::filesystem::path sourcePath = localPath.toStdString();
    if (!std::filesystem::exists(sourcePath)) {
        ++m_uploadBatchCompleted;
        ++m_uploadBatchFailed;

        if (!m_pendingUploadQueue.isEmpty()) {
            startNextQueuedUpload();
            return;
        }

        setBusy(false);
        setStatusMessage(m_uploadBatchTotal <= 1
            ? QStringLiteral("The selected local file or folder does not exist anymore.")
            : QStringLiteral("Uploaded %1 of %2 files (%3 failed).")
                .arg(m_uploadBatchCompleted - m_uploadBatchFailed)
                .arg(m_uploadBatchTotal)
                .arg(m_uploadBatchFailed));
        m_pendingAction = PendingAction::None;
        m_uploadBatchActive = false;
        m_uploadBatchTotal = 0;
        m_uploadBatchCompleted = 0;
        m_uploadBatchFailed = 0;
        m_pendingUploadQueue.clear();
        m_pendingLocalPath.clear();
        m_activeEntryPath.clear();
        m_activeEntryName.clear();
        startNextQueuedAction();
        return;
    }

    m_pendingLocalPath = localPath;
    m_pendingEntryPath.clear();
    m_activeEntryPath = normalizeRemotePath(localPath);
    m_activeEntryName = QString::fromStdString(sourcePath.filename().string());
    m_pendingAction = PendingAction::Upload;
    setTransferProgress(0.0);

    auto& module = ModulesManager::GetModuleReference<FileShareModule>();
    if (module->GetModuleState() != ModuleState::Enabled) {
        m_waitingForModule = true;
        setBusy(true);
        setStatusMessage(QStringLiteral("Preparing upload channels..."));
        module->Enable(true);
        return;
    }

    startPendingActionIfReady();
}

void FileManagerController::loadDirectory(const QString& remotePath)
{
    const QString normalizedPath = normalizeRemotePath(remotePath);
    if (normalizedPath.isEmpty()) {
        setStatusMessage(QStringLiteral("Enter a valid path on the connected device."));
        return;
    }

    setCurrentRemotePath(normalizedPath);
    setBusy(true);
    setStatusMessage(QStringLiteral("Loading %1...").arg(normalizedPath));

    auto& module = ModulesManager::GetModuleReference<FileShareModule>();
    if (module->GetModuleState() != ModuleState::Enabled) {
        m_waitingForModule = true;
        m_pendingAction = PendingAction::Browse;
        m_pendingBrowsePath = normalizedPath;
        module->Enable(true);
        return;
    }

    module->FetchDirectoryEntries(normalizedPath.toStdString());
}

void FileManagerController::setCurrentRemotePath(const QString& currentRemotePath)
{
    if (m_currentRemotePath == currentRemotePath) {
        return;
    }

    m_currentRemotePath = currentRemotePath;
    emit currentRemotePathChanged();
}

void FileManagerController::setRemoteEntries(const QVariantList& remoteEntries)
{
    if (m_remoteEntries == remoteEntries) {
        return;
    }

    m_remoteEntries = remoteEntries;
    emit remoteEntriesChanged();
}

void FileManagerController::setBusy(const bool busy)
{
    if (m_busy == busy) {
        return;
    }

    m_busy = busy;
    emit busyChanged();
}

void FileManagerController::setTransferProgress(const double transferProgress)
{
    if (qFuzzyCompare(m_transferProgress, transferProgress)) {
        return;
    }

    m_transferProgress = transferProgress;
    emit transferProgressChanged();
}

void FileManagerController::setStatusMessage(const QString& statusMessage)
{
    if (m_statusMessage == statusMessage) {
        return;
    }

    m_statusMessage = statusMessage;
    emit statusMessageChanged();
}

QString FileManagerController::normalizeRemotePath(const QString& path)
{
    QString normalized = path.trimmed();
    if (normalized.isEmpty()) {
        return QString();
    }

    normalized.replace('\\', '/');
    while (normalized.contains(QStringLiteral("//"))) {
        normalized.replace(QStringLiteral("//"), QStringLiteral("/"));
    }

    if (normalized.length() > 1 && normalized.endsWith('/')) {
        normalized.chop(1);
    }

    return normalized;
}

QString FileManagerController::parentRemotePath(const QString& path)
{
    const QString normalized = normalizeRemotePath(path);
    if (normalized.isEmpty() || normalized == QStringLiteral("/")) {
        return normalized;
    }

    const int separatorIndex = normalized.lastIndexOf('/');
    if (separatorIndex <= 0) {
        return QStringLiteral("/");
    }

    return normalized.left(separatorIndex);
}

QString FileManagerController::composeRemotePath(const FileEntry& entry)
{
    const QString basePath = QString::fromStdString(entry.GetPath().value_or(std::string()));
    const QString name = QString::fromStdString(entry.GetName().value_or(std::string()));
    if (basePath.isEmpty() || name.isEmpty()) {
        return QString();
    }

    return normalizeRemotePath(basePath + QStringLiteral("/") + name);
}

QVariantMap FileManagerController::toVariantMap(const FileEntry& entry)
{
    QVariantMap item;
    const QString fullPath = composeRemotePath(entry);
    const bool isDirectory = entry.GetType().has_value() && entry.GetType().value() == FileType::Directory;

    item.insert(QStringLiteral("name"), QString::fromStdString(entry.GetName().value_or(std::string())));
    item.insert(QStringLiteral("path"), fullPath);
    item.insert(QStringLiteral("isDirectory"), isDirectory);
    item.insert(QStringLiteral("size"), static_cast<qulonglong>(entry.GetSize().value_or(0)));
    item.insert(QStringLiteral("sizeLabel"), formatEntrySize(entry));
    item.insert(QStringLiteral("typeLabel"), fileTypeLabel(entry.GetType()));
    item.insert(QStringLiteral("iconSource"), fileTypeIconSource(entry.GetType()));
    return item;
}
