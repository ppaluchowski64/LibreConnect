#pragma once

#include <QObject>
#include <QEvent>
#include <QPointer>
#include <QTimer>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include <unordered_map>

#include <FileEntry.h>

class FileManagerController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString currentRemotePath READ currentRemotePath NOTIFY currentRemotePathChanged)
    Q_PROPERTY(QString localDownloadDirectory READ localDownloadDirectory NOTIFY localDownloadDirectoryChanged)
    Q_PROPERTY(QVariantList remoteEntries READ remoteEntries NOTIFY remoteEntriesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double transferProgress READ transferProgress NOTIFY transferProgressChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    explicit FileManagerController(QObject* parent = nullptr);

    QString currentRemotePath() const { return m_currentRemotePath; }
    QString localDownloadDirectory() const { return m_localDownloadDirectory; }
    QVariantList remoteEntries() const { return m_remoteEntries; }
    bool busy() const { return m_busy; }
    double transferProgress() const { return m_transferProgress; }
    QString statusMessage() const { return m_statusMessage; }

    Q_INVOKABLE void setLocalDownloadDirectory(const QUrl& directoryUrl);
    Q_INVOKABLE void refreshEntries();
    Q_INVOKABLE void browseTo(const QString& remotePath);
    Q_INVOKABLE void goUp();
    Q_INVOKABLE void downloadEntry(const QString& remotePath);

protected:
    bool event(QEvent* event) override;

signals:
    void currentRemotePathChanged();
    void localDownloadDirectoryChanged();
    void remoteEntriesChanged();
    void busyChanged();
    void transferProgressChanged();
    void statusMessageChanged();

private:
    enum class PendingAction {
        None,
        Browse,
        Download
    };

    void refreshModuleState();
    void startPendingActionIfReady();
    void loadDirectory(const QString& remotePath);
    void setCurrentRemotePath(const QString& currentRemotePath);
    void setRemoteEntries(const QVariantList& remoteEntries);
    void setBusy(bool busy);
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
    QString m_activeEntryPath;
    QString m_activeEntryName;
    QVariantList m_remoteEntries;
    std::unordered_map<std::string, FileEntry> m_entryLookup;
    bool m_busy = false;
    bool m_waitingForModule = false;
    double m_transferProgress = 0.0;
    PendingAction m_pendingAction = PendingAction::None;
};
