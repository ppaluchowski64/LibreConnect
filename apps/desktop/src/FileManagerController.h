#pragma once

#include <QObject>
#include <QEvent>
#include <QPointer>
#include <QTimer>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QStringList>
#include <QList>

#include <unordered_map>

#include <FileEntry.h>

class FileManagerController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString currentRemotePath READ currentRemotePath NOTIFY currentRemotePathChanged)
    Q_PROPERTY(QString localDownloadDirectory READ localDownloadDirectory NOTIFY localDownloadDirectoryChanged)
    Q_PROPERTY(QVariantList remoteEntries READ remoteEntries NOTIFY remoteEntriesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool dragExportInProgress READ dragExportInProgress NOTIFY dragExportInProgressChanged)
    Q_PROPERTY(double transferProgress READ transferProgress NOTIFY transferProgressChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    explicit FileManagerController(QObject* parent = nullptr);

    QString currentRemotePath() const { return m_currentRemotePath; }
    QString localDownloadDirectory() const { return m_localDownloadDirectory; }
    QVariantList remoteEntries() const { return m_remoteEntries; }
    bool busy() const { return m_busy; }
    bool dragExportInProgress() const { return m_dragExportInProgress; }
    double transferProgress() const { return m_transferProgress; }
    QString statusMessage() const { return m_statusMessage; }

    Q_INVOKABLE void setLocalDownloadDirectory(const QUrl& directoryUrl);
    Q_INVOKABLE void refreshEntries();
    Q_INVOKABLE void browseTo(const QString& remotePath);
    Q_INVOKABLE void goUp();
    Q_INVOKABLE void downloadEntry(const QString& remotePath);
    Q_INVOKABLE void downloadEntries(const QStringList& remotePaths);
    Q_INVOKABLE void openEntry(const QString& remotePath);
    Q_INVOKABLE void copyEntry(const QString& remotePath);
    Q_INVOKABLE void copyEntries(const QStringList& remotePaths);
    Q_INVOKABLE void beginExternalDrag(const QStringList& remotePaths);
    Q_INVOKABLE void uploadLocalEntry(const QUrl& localPathUrl);

protected:
    bool event(QEvent* event) override;

signals:
    void currentRemotePathChanged();
    void localDownloadDirectoryChanged();
    void remoteEntriesChanged();
    void busyChanged();
    void dragExportInProgressChanged();
    void transferProgressChanged();
    void statusMessageChanged();

private:
    enum class PendingAction {
        None,
        Browse,
        Download,
        Open,
        Copy,
        Upload,
        DragExport
    };

    void refreshModuleState();
    void startNextQueuedAction();
    void startPendingActionIfReady();
    void beginDownloadForPath(const QString& normalizedPath, bool partOfBatch);
    void startNextQueuedDownload();
    void startNextQueuedUpload();
    void beginUploadForLocalPath(const QString& localPath);
    void loadDirectory(const QString& remotePath);
    void setCurrentRemotePath(const QString& currentRemotePath);
    void setRemoteEntries(const QVariantList& remoteEntries);
    void setBusy(bool busy);
    void setDragExportInProgress(bool dragExportInProgress);
    void setTransferProgress(double transferProgress);
    void setStatusMessage(const QString& statusMessage);
    static QString normalizeRemotePath(const QString& path);
    static QString parentRemotePath(const QString& path);
    static QString composeRemotePath(const FileEntry& entry);
    static QVariantMap toVariantMap(const FileEntry& entry);

    QTimer m_pollTimer;
    QString m_currentRemotePath = QStringLiteral("/storage/emulated/0");
    QString m_localDownloadDirectory;
    QString m_statusMessage = QStringLiteral("Browse files on the connected device.");
    QString m_pendingEntryPath;
    QString m_pendingBrowsePath;
    QString m_pendingLocalPath;
    QString m_activeEntryPath;
    QString m_activeEntryName;
    QStringList m_pendingCopyPaths;
    QStringList m_pendingOpenQueue;
    QList<QStringList> m_pendingCopyQueue;
    QStringList m_pendingDownloadQueue;
    QStringList m_pendingUploadQueue;
    QVariantList m_remoteEntries;
    std::unordered_map<std::string, FileEntry> m_entryLookup;
    bool m_busy = false;
    bool m_waitingForModule = false;
    bool m_downloadBatchActive = false;
    bool m_uploadBatchActive = false;
    bool m_dragExportInProgress = false;
    int m_downloadBatchTotal = 0;
    int m_downloadBatchCompleted = 0;
    int m_downloadBatchFailed = 0;
    int m_uploadBatchTotal = 0;
    int m_uploadBatchCompleted = 0;
    int m_uploadBatchFailed = 0;
    double m_transferProgress = 0.0;
    PendingAction m_pendingAction = PendingAction::None;
};
