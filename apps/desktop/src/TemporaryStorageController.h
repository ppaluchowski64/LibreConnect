#pragma once

#include <QObject>
#include <QString>

class TemporaryStorageController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString temporaryStoragePath READ temporaryStoragePath NOTIFY temporaryStoragePathChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    explicit TemporaryStorageController(QObject* parent = nullptr);

    QString temporaryStoragePath() const { return m_temporaryStoragePath; }
    QString statusMessage() const { return m_statusMessage; }

    Q_INVOKABLE bool clearTemporaryStorage();

signals:
    void temporaryStoragePathChanged();
    void statusMessageChanged();

private:
    void refreshTemporaryStoragePath();
    void setTemporaryStoragePath(const QString& temporaryStoragePath);
    void setStatusMessage(const QString& statusMessage);

    QString m_temporaryStoragePath;
    QString m_statusMessage;
};
