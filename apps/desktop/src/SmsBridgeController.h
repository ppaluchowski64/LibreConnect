#pragma once

#include <QObject>
#include <QEvent>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QString>
#include <QHash>
#include <QSet>
#include <QVector>
#include <vector>

class SmsBridgeController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QVariantList contacts READ contacts NOTIFY contactsChanged)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(QString selectedContactNumber READ selectedContactNumber NOTIFY selectedConversationChanged)
    Q_PROPERTY(QString selectedContactName READ selectedContactName NOTIFY selectedConversationChanged)
    Q_PROPERTY(bool canSend READ canSend NOTIFY canSendChanged)

public:
    explicit SmsBridgeController(QObject* parent = nullptr);

    bool connected() const { return m_connected; }
    bool ready() const { return m_ready; }
    bool busy() const { return m_busy; }
    QString statusMessage() const { return m_statusMessage; }
    QVariantList contacts() const;
    QVariantList messages() const;
    QString selectedContactNumber() const;
    QString selectedContactName() const { return m_selectedContactName; }
    bool canSend() const;

    Q_INVOKABLE void refreshConversations();
    Q_INVOKABLE void selectConversation(const QString& phoneNumber, const QString& contactName);
    Q_INVOKABLE void sendMessage(const QString& text);

signals:
    void connectedChanged();
    void readyChanged();
    void busyChanged();
    void statusMessageChanged();
    void contactsChanged();
    void messagesChanged();
    void selectedConversationChanged();
    void canSendChanged();

protected:
    bool event(QEvent* event) override;

private:
    struct ContactState {
        QString name;
        QString number;
        QString key;
        QString preview;
        qint64 lastTimestamp = 0;
        int unread = 0;
        bool loading = false;
    };

    struct MessageState {
        QString id;
        QString body;
        bool incoming = true;
        bool pending = false;
        bool failed = false;
        qint64 timestamp = 0;
    };

    static QString normalizeNumber(const QString& number);
    static MessageState parseMessage(const QString& rawMessage, const QString& key, int index, qint64 defaultTimestamp);
    static QString buildPreview(const QString& body);
    static QString generateLocalMessageId();

    void refreshState();
    void ensureModuleEnabled();
    void setBusy(bool busy);
    void setReady(bool ready);
    void setStatusMessage(const QString& statusMessage);
    void ensureContactExists(const QString& key, const QString& displayName, const QString& dialNumber);
    int findContactIndex(const QString& key) const;
    void sortContacts();
    void emitConversationChanged();
    void updateSelectedMessages();
    QVariantList buildMessagesVariant(const QString& key) const;
    void requestMmsContentFetches(const std::vector<std::string>& rawMessages) const;
    ContactState* findContact(const QString& key);
    const ContactState* findContact(const QString& key) const;

    QTimer m_pollTimer;
    bool m_connected = false;
    bool m_ready = false;
    bool m_busy = false;
    QString m_statusMessage;
    QVector<ContactState> m_contacts;
    QHash<QString, QVector<MessageState>> m_messagesByContact;
    QString m_selectedContactKey;
    QString m_selectedContactName;
    QHash<QString, QString> m_pendingMessageConversationById;
    mutable QSet<QString> m_requestedMmsContentTargets;
    qint64 m_timestampCounter = 0;
};
