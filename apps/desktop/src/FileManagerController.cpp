#include "FileManagerController.h"

#include <QPointer>
#include <QStandardPaths>

#include <algorithm>

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
    const QString normalizedPath = normalizeRemotePath(remotePath);
    if (normalizedPath.isEmpty()) {
        setStatusMessage(QStringLiteral("Select a file or folder first."));
        return;
    }

    if (m_localDownloadDirectory.isEmpty()) {
        setStatusMessage(QStringLiteral("Choose a local download folder first."));
        return;
    }

    auto lookup = m_entryLookup.find(normalizedPath.toStdString());
    if (lookup == m_entryLookup.end()) {
        setStatusMessage(QStringLiteral("That entry is no longer available in the current folder."));
        return;
    }

    m_pendingEntryPath = normalizedPath;
    m_activeEntryPath = normalizedPath;
    m_activeEntryName = QString::fromStdString(lookup->second.GetName().value_or(std::string()));
    m_pendingAction = PendingAction::Download;
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

bool FileManagerController::event(QEvent* event)
{
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
        return true;
    }

    if (event->type() == EntryTransferProgressEvent::Type) {
        auto* progressEvent = static_cast<EntryTransferProgressEvent*>(event);
        if (progressEvent->GetOperation() != TransferOperation::Fetch) {
            return true;
        }

        const QString entryPath = composeRemotePath(progressEvent->GetFileEntry());
        if (!m_activeEntryPath.isEmpty() && entryPath != m_activeEntryPath) {
            return true;
        }

        const size_t totalBytes = progressEvent->GetTotalBytes();
        const size_t transferredBytes = progressEvent->GetBytesTransferred();
        const double progress = totalBytes == 0 ? 0.0 : static_cast<double>(transferredBytes) / static_cast<double>(totalBytes);
        setTransferProgress(progress);
        setBusy(true);
        setStatusMessage(QStringLiteral("Downloading %1 (%2%)").arg(
            m_activeEntryName,
            QString::number(progress * 100.0, 'f', 0)));
        return true;
    }

    if (event->type() == EntryTransferResultEvent::Type) {
        auto* resultEvent = static_cast<EntryTransferResultEvent*>(event);
        const QString entryPath = composeRemotePath(resultEvent->GetFileEntry());
        if (!m_activeEntryPath.isEmpty() && entryPath != m_activeEntryPath) {
            return true;
        }

        setBusy(false);
        setTransferProgress(resultEvent->Success() ? 1.0 : 0.0);
        setStatusMessage(resultEvent->Success()
            ? QStringLiteral("Downloaded %1 to %2.").arg(m_activeEntryName, m_localDownloadDirectory)
            : QStringLiteral("Failed to download %1.").arg(m_activeEntryName));

        m_pendingEntryPath.clear();
        m_activeEntryPath.clear();
        m_activeEntryName.clear();
        m_waitingForModule = false;
        m_pendingAction = PendingAction::None;
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

void FileManagerController::startPendingActionIfReady()
{
    if (!m_waitingForModule || m_pendingAction == PendingAction::None) {
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

    if (m_pendingEntryPath.isEmpty()) {
        return;
    }

    auto lookup = m_entryLookup.find(m_pendingEntryPath.toStdString());
    if (lookup == m_entryLookup.end()) {
        m_waitingForModule = false;
        m_pendingAction = PendingAction::None;
        setBusy(false);
        setStatusMessage(QStringLiteral("The selected entry is no longer available."));
        return;
    }

    m_waitingForModule = false;
    setBusy(true);
    setStatusMessage(QStringLiteral("Downloading %1...").arg(m_activeEntryName));
    module->FetchEntry(lookup->second, m_localDownloadDirectory.toStdString());
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
    item.insert(QStringLiteral("typeLabel"), fileTypeLabel(entry.GetType()));
    return item;
}
