#include "SmsBridgeController.h"

#include <QPointer>
#include <QUuid>
#include <QDateTime>
#include <algorithm>

#include <boost/uuid/uuid_io.hpp>

#include <ConnectionManager.h>
#include <Events.h>
#include <ModulesManager.h>
#include <SmsBridgeEvents.h>

namespace {
constexpr int POLL_INTERVAL_MS = 400;

bool parseTimestampedMessage(
    const QString& rawMessage,
    const QString& prefix,
    const bool trimTrailingSection,
    int& messageType,
    qint64& timestamp,
    QString& body
)
{
    if (!rawMessage.startsWith(prefix)) {
        return false;
    }

    const int prefixLength = prefix.size();
    const int firstSeparator = rawMessage.indexOf(QLatin1Char('|'), prefixLength);
    const int secondSeparator = firstSeparator < 0 ? -1 : rawMessage.indexOf(QLatin1Char('|'), firstSeparator + 1);
    const int lastSeparator = trimTrailingSection ? rawMessage.lastIndexOf(QLatin1Char('|')) : -1;

    if (firstSeparator <= prefixLength || secondSeparator <= firstSeparator) {
        body = rawMessage.mid(prefixLength);
        return true;
    }

    bool typeOk = false;
    bool timestampOk = false;
    const int parsedType = rawMessage.mid(prefixLength, firstSeparator - prefixLength).toInt(&typeOk);
    const qint64 parsedTimestamp = rawMessage.mid(firstSeparator + 1, secondSeparator - firstSeparator - 1).toLongLong(&timestampOk);

    const int bodyEnd = trimTrailingSection && lastSeparator > secondSeparator
        ? lastSeparator
        : rawMessage.size();
    body = rawMessage.mid(secondSeparator + 1, bodyEnd - secondSeparator - 1);

    if (typeOk) {
        messageType = parsedType;
    }

    if (timestampOk && parsedTimestamp > 0) {
        timestamp = parsedTimestamp;
    }

    return true;
}
}

SmsBridgeController::SmsBridgeController(QObject* parent)
    : QObject(parent)
{
    ConnectionManager::AddEventListener(QPointer<QObject>(this));

    m_pollTimer.setInterval(POLL_INTERVAL_MS);
    connect(&m_pollTimer, &QTimer::timeout, this, &SmsBridgeController::refreshState);
    m_pollTimer.start();

    refreshState();
}

QVariantList SmsBridgeController::contacts() const
{
    QVariantList result;
    result.reserve(m_contacts.size());

    for (const ContactState& contact : m_contacts) {
        QVariantMap map;
        map.insert(QStringLiteral("name"), contact.name);
        map.insert(QStringLiteral("number"), contact.number);
        map.insert(QStringLiteral("key"), contact.key);
        map.insert(QStringLiteral("preview"), contact.preview);
        map.insert(QStringLiteral("lastTimestamp"), contact.lastTimestamp);
        map.insert(QStringLiteral("unread"), contact.unread);
        map.insert(QStringLiteral("loading"), contact.loading);
        map.insert(QStringLiteral("selected"), contact.key == m_selectedContactKey);
        result.push_back(map);
    }

    return result;
}

QVariantList SmsBridgeController::messages() const
{
    return buildMessagesVariant(m_selectedContactKey);
}

QString SmsBridgeController::selectedContactNumber() const
{
    const ContactState* contact = findContact(m_selectedContactKey);
    return contact == nullptr ? QString() : contact->number;
}

bool SmsBridgeController::canSend() const
{
    return m_connected && m_ready && !m_selectedContactKey.isEmpty();
}

void SmsBridgeController::refreshConversations()
{
    if (!m_connected) {
        setStatusMessage(QStringLiteral("Connect to a mobile device to load messages."));
        return;
    }

    ensureModuleEnabled();
    setBusy(true);
    setStatusMessage(QStringLiteral("Fetching conversations from phone..."));

    auto& module = ModulesManager::GetModuleReference<SmsBridgeModule>();
    module->GetContactList();
}

void SmsBridgeController::selectConversation(const QString& phoneNumber, const QString& contactName)
{
    const QString key = normalizeNumber(phoneNumber);
    if (key.isEmpty()) {
        return;
    }

    m_selectedContactKey = key;
    if (!contactName.trimmed().isEmpty()) {
        m_selectedContactName = contactName.trimmed();
    } else {
        const ContactState* contact = findContact(key);
        m_selectedContactName = contact == nullptr ? phoneNumber : contact->name;
    }

    if (ContactState* contact = findContact(key)) {
        contact->unread = 0;
    }

    emitConversationChanged();
    emit contactsChanged();

    auto& module = ModulesManager::GetModuleReference<SmsBridgeModule>();
    const ContactState* selected = findContact(key);
    if (selected != nullptr && !selected->number.isEmpty()) {
        module->GetTargetMessages(selected->number.toStdString());
        if (ContactState* selectedMutable = findContact(key)) {
            selectedMutable->loading = true;
        }
        emit contactsChanged();
    }
}

void SmsBridgeController::sendMessage(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    if (!canSend()) {
        setStatusMessage(QStringLiteral("Select a conversation and ensure phone is connected."));
        return;
    }

    ContactState* contact = findContact(m_selectedContactKey);
    if (contact == nullptr || contact->number.isEmpty()) {
        setStatusMessage(QStringLiteral("Selected contact has no phone number."));
        return;
    }

    auto& module = ModulesManager::GetModuleReference<SmsBridgeModule>();
    const uuid messageId = module->SendSMS(contact->number.toStdString(), trimmed.toStdString());
    const QString messageIdString = QString::fromStdString(boost::uuids::to_string(messageId));

    MessageState message;
    message.id = messageIdString;
    message.body = trimmed;
    message.incoming = false;
    message.pending = true;
    message.failed = false;
    message.timestamp = QDateTime::currentMSecsSinceEpoch();

    m_messagesByContact[m_selectedContactKey].push_back(message);
    m_pendingMessageConversationById.insert(messageIdString, m_selectedContactKey);

    contact->preview = buildPreview(trimmed);
    contact->lastTimestamp = message.timestamp;
    contact->loading = false;

    sortContacts();
    emit contactsChanged();
    updateSelectedMessages();
    setStatusMessage(QStringLiteral("Sending message..."));
}

bool SmsBridgeController::event(QEvent* event)
{
    if (event->type() == ConnectedEvent::Type) {
        const auto* connectedEvent = static_cast<ConnectedEvent*>(event);
        const bool connectedNow = connectedEvent->GetResult() == EventResult::SUCCESS;

        if (m_connected != connectedNow) {
            m_connected = connectedNow;
            emit connectedChanged();
            emit canSendChanged();
        }

        if (m_connected) {
            ensureModuleEnabled();
            refreshConversations();
        } else {
            setReady(false);
            setBusy(false);
            setStatusMessage(QStringLiteral("Connect to a mobile device to load messages."));
        }

        return true;
    }

    if (event->type() == DisconnectedEvent::Type) {
        if (m_connected) {
            m_connected = false;
            emit connectedChanged();
            emit canSendChanged();
        }
        setReady(false);
        setBusy(false);
        setStatusMessage(QStringLiteral("Phone disconnected."));
        return true;
    }

    if (event->type() == FetchContactListResultEvent::Type) {
        const auto* contactsEvent = static_cast<FetchContactListResultEvent*>(event);
        auto& module = ModulesManager::GetModuleReference<SmsBridgeModule>();

        for (const auto& entry : contactsEvent->GetContacts()) {
            const QString name = QString::fromStdString(entry.first).trimmed();
            const QString number = QString::fromStdString(entry.second).trimmed();
            const QString key = normalizeNumber(number);
            if (key.isEmpty()) {
                continue;
            }

            ensureContactExists(key, name, number);
            if (ContactState* contact = findContact(key)) {
                contact->loading = true;
            }

            module->GetTargetMessages(number.toStdString());
        }

        sortContacts();
        if (m_selectedContactKey.isEmpty() && !m_contacts.isEmpty()) {
            m_selectedContactKey = m_contacts.front().key;
            m_selectedContactName = m_contacts.front().name;
            emit selectedConversationChanged();
            emit canSendChanged();
        }

        setBusy(false);
        setStatusMessage(QStringLiteral("Conversations loaded."));
        emit contactsChanged();
        updateSelectedMessages();
        return true;
    }

    if (event->type() == FetchMessageListResultEvent::Type) {
        const auto* messagesEvent = static_cast<FetchMessageListResultEvent*>(event);
        const QString number = QString::fromStdString(messagesEvent->GetNumber()).trimmed();
        const QString key = normalizeNumber(number);
        if (key.isEmpty()) {
            return true;
        }

        // Message fetch payloads only carry the number; keep any existing contact name.
        ensureContactExists(key, QString(), number);
        QVector<MessageState> parsed;
        const auto& rawMessages = messagesEvent->GetMessages();
        parsed.reserve(static_cast<qsizetype>(rawMessages.size()));
        for (int index = static_cast<int>(rawMessages.size()) - 1; index >= 0; --index) {
            parsed.push_back(parseMessage(
                QString::fromStdString(rawMessages.at(static_cast<size_t>(index))),
                key,
                index,
                ++m_timestampCounter
            ));
        }

        m_messagesByContact.insert(key, parsed);

        if (ContactState* contact = findContact(key)) {
            contact->loading = false;
            if (!parsed.isEmpty()) {
                contact->preview = buildPreview(parsed.back().body);
                contact->lastTimestamp = parsed.back().timestamp;
            } else if (contact->preview.isEmpty()) {
                contact->preview = QStringLiteral("No messages yet");
            }

            if (m_selectedContactKey == key) {
                contact->unread = 0;
            }
        }

        sortContacts();
        emit contactsChanged();
        updateSelectedMessages();
        return true;
    }

    if (event->type() == SendSmsResultEvent::Type) {
        const auto* sendResultEvent = static_cast<SendSmsResultEvent*>(event);
        const QString id = QString::fromStdString(boost::uuids::to_string(sendResultEvent->GetMessageUUID()));
        const QString conversationKey = m_pendingMessageConversationById.take(id);
        if (conversationKey.isEmpty()) {
            return true;
        }

        QVector<MessageState>& conversation = m_messagesByContact[conversationKey];
        for (MessageState& message : conversation) {
            if (message.id != id) {
                continue;
            }

            message.pending = false;
            message.failed = !sendResultEvent->Success();
            break;
        }

        setStatusMessage(sendResultEvent->Success()
            ? QStringLiteral("Message sent.")
            : QStringLiteral("Failed to send message."));
        emit contactsChanged();
        updateSelectedMessages();
        return true;
    }

    if (event->type() == NewSmsReceivedEvent::Type) {
        const auto* newSmsEvent = static_cast<NewSmsReceivedEvent*>(event);
        const QString sender = QString::fromStdString(newSmsEvent->GetSender()).trimmed();
        const QString body = QString::fromStdString(newSmsEvent->GetBody());
        const QString key = normalizeNumber(sender);
        if (key.isEmpty()) {
            return true;
        }

        // Incoming SMS events only include sender address, so do not overwrite known names.
        ensureContactExists(key, QString(), sender);
        MessageState message;
        message.id = generateLocalMessageId();
        message.body = body;
        message.incoming = true;
        message.pending = false;
        message.failed = false;
        const int64_t receivedTimestamp = newSmsEvent->GetTimestamp();
        message.timestamp = receivedTimestamp > 0
            ? static_cast<qint64>(receivedTimestamp)
            : QDateTime::currentMSecsSinceEpoch();
        m_messagesByContact[key].push_back(message);

        if (ContactState* contact = findContact(key)) {
            contact->preview = buildPreview(body);
            contact->lastTimestamp = message.timestamp;
            contact->loading = false;
            if (m_selectedContactKey != key) {
                contact->unread += 1;
            } else {
                contact->unread = 0;
            }
        }

        sortContacts();
        emit contactsChanged();
        updateSelectedMessages();
        setStatusMessage(QStringLiteral("New message received."));
        return true;
    }

    return QObject::event(event);
}

QString SmsBridgeController::normalizeNumber(const QString& number)
{
    const QString trimmed = number.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    bool hasLetters = false;
    for (const QChar c : trimmed) {
        if (c.isLetter()) {
            hasLetters = true;
            break;
        }
    }

    QString normalized;
    normalized.reserve(number.size());

    bool plusUsed = false;
    for (const QChar c : trimmed) {
        if (c.isDigit()) {
            normalized.append(c);
            continue;
        }

        if (c == QLatin1Char('+') && !plusUsed && normalized.isEmpty()) {
            normalized.append(c);
            plusUsed = true;
        }
    }

    if (!hasLetters && !normalized.isEmpty()) {
        return normalized;
    }

    return trimmed.simplified().toLower();
}

SmsBridgeController::MessageState SmsBridgeController::parseMessage(
    const QString& rawMessage,
    const QString& key,
    const int index,
    const qint64 defaultTimestamp
)
{
    MessageState parsed;
    parsed.timestamp = defaultTimestamp;

    int smsType = 1;
    if (parseTimestampedMessage(
            rawMessage,
            QStringLiteral("v2|"),
            false,
            smsType,
            parsed.timestamp,
            parsed.body
        ) || parseTimestampedMessage(
            rawMessage,
            QStringLiteral("mms|"),
            true,
            smsType,
            parsed.timestamp,
            parsed.body
        )) {
    } else if (!rawMessage.isEmpty() && rawMessage.at(0).isDigit()) {
        smsType = rawMessage.at(0).digitValue();
        parsed.body = rawMessage.mid(1);
    } else {
        parsed.body = rawMessage;
    }

    parsed.id = QStringLiteral("%1:%2:%3").arg(key).arg(parsed.timestamp).arg(index);
    parsed.incoming = smsType == 1 || smsType == 0;
    parsed.pending = false;
    parsed.failed = false;
    return parsed;
}

QString SmsBridgeController::buildPreview(const QString& body)
{
    const QString simplified = body.simplified();
    if (simplified.isEmpty()) {
        return QStringLiteral("Empty message");
    }

    if (simplified.size() <= 80) {
        return simplified;
    }

    return simplified.left(77) + QStringLiteral("...");
}

QString SmsBridgeController::generateLocalMessageId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void SmsBridgeController::refreshState()
{
    if (!m_connected) {
        setReady(false);
        return;
    }

    auto& module = ModulesManager::GetModuleReference<SmsBridgeModule>();
    const ModuleState state = module->GetModuleState();

    if (state == ModuleState::Disabled) {
        setReady(false);
        module->Enable(true);
        return;
    }

    setReady(state == ModuleState::Enabling || state == ModuleState::Enabled);
}

void SmsBridgeController::ensureModuleEnabled()
{
    if (!m_connected) {
        return;
    }

    auto& module = ModulesManager::GetModuleReference<SmsBridgeModule>();
    const ModuleState state = module->GetModuleState();
    if (state == ModuleState::Disabled) {
        module->Enable(true);
    }
}

void SmsBridgeController::setBusy(const bool busy)
{
    if (m_busy == busy) {
        return;
    }

    m_busy = busy;
    emit busyChanged();
}

void SmsBridgeController::setReady(const bool ready)
{
    if (m_ready == ready) {
        return;
    }

    m_ready = ready;
    emit readyChanged();
    emit canSendChanged();
}

void SmsBridgeController::setStatusMessage(const QString& statusMessage)
{
    if (m_statusMessage == statusMessage) {
        return;
    }

    m_statusMessage = statusMessage;
    emit statusMessageChanged();
}

void SmsBridgeController::ensureContactExists(const QString& key, const QString& displayName, const QString& dialNumber)
{
    ContactState* existing = findContact(key);
    if (existing != nullptr) {
        if (!displayName.trimmed().isEmpty()) {
            existing->name = displayName.trimmed();
        } else if (existing->name.isEmpty()) {
            existing->name = dialNumber;
        }

        if (!dialNumber.trimmed().isEmpty()) {
            existing->number = dialNumber.trimmed();
        }
        return;
    }

    ContactState created;
    created.key = key;
    created.name = displayName.trimmed().isEmpty() ? dialNumber.trimmed() : displayName.trimmed();
    created.number = dialNumber.trimmed();
    created.preview = QStringLiteral("No messages yet");
    created.lastTimestamp = 0;
    created.unread = 0;
    created.loading = false;
    m_contacts.push_back(created);
}

int SmsBridgeController::findContactIndex(const QString& key) const
{
    for (int i = 0; i < m_contacts.size(); ++i) {
        if (m_contacts.at(i).key == key) {
            return i;
        }
    }

    return -1;
}

void SmsBridgeController::sortContacts()
{
    std::sort(m_contacts.begin(), m_contacts.end(), [](const ContactState& lhs, const ContactState& rhs) {
        if (lhs.lastTimestamp == rhs.lastTimestamp) {
            return lhs.name.toLower() < rhs.name.toLower();
        }
        return lhs.lastTimestamp > rhs.lastTimestamp;
    });
}

void SmsBridgeController::emitConversationChanged()
{
    emit selectedConversationChanged();
    emit canSendChanged();
    updateSelectedMessages();
}

void SmsBridgeController::updateSelectedMessages()
{
    emit messagesChanged();
}

QVariantList SmsBridgeController::buildMessagesVariant(const QString& key) const
{
    QVariantList result;
    if (key.isEmpty()) {
        return result;
    }

    const QVector<MessageState> conversation = m_messagesByContact.value(key);
    result.reserve(conversation.size());

    for (const MessageState& message : conversation) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), message.id);
        map.insert(QStringLiteral("body"), message.body);
        map.insert(QStringLiteral("incoming"), message.incoming);
        map.insert(QStringLiteral("pending"), message.pending);
        map.insert(QStringLiteral("failed"), message.failed);
        map.insert(QStringLiteral("timestamp"), message.timestamp);
        result.push_back(map);
    }

    return result;
}

SmsBridgeController::ContactState* SmsBridgeController::findContact(const QString& key)
{
    const int index = findContactIndex(key);
    return index < 0 ? nullptr : &m_contacts[index];
}

const SmsBridgeController::ContactState* SmsBridgeController::findContact(const QString& key) const
{
    const int index = findContactIndex(key);
    return index < 0 ? nullptr : &m_contacts[index];
}
