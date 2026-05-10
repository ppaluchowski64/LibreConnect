#include "SmsBridgeController.h"

#include <QPointer>
#include <QUuid>
#include <QDateTime>
#include <QImageReader>
#include <QUrl>
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

QStringList extractMmsAttachmentTargets(const QString& rawMessage)
{
    if (!rawMessage.startsWith(QStringLiteral("mms|"))) {
        return {};
    }

    const int firstSeparator = rawMessage.indexOf(QLatin1Char('|'), 4);
    const int secondSeparator = firstSeparator < 0 ? -1 : rawMessage.indexOf(QLatin1Char('|'), firstSeparator + 1);
    const int lastSeparator = rawMessage.lastIndexOf(QLatin1Char('|'));
    if (firstSeparator <= 4 || secondSeparator <= firstSeparator || lastSeparator <= secondSeparator) {
        return {};
    }

    const QString attachmentsSection = rawMessage.mid(lastSeparator + 1).trimmed();
    if (attachmentsSection.isEmpty()) {
        return {};
    }

    QStringList targets;
    for (const QString& candidate : attachmentsSection.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const QString trimmed = candidate.trimmed();
        if (!trimmed.isEmpty()) {
            targets.push_back(trimmed);
        }
    }

    return targets;
}

struct ContactSummary {
    QString name;
    QString preview;
    qint64 lastTimestamp = 0;
};

ContactSummary parseContactSummary(const QString& rawName)
{
    ContactSummary summary;
    summary.name = rawName.trimmed();

    if (!rawName.startsWith(QStringLiteral("summary|"))) {
        return summary;
    }

    const int timestampStart = QStringLiteral("summary|").size();
    const int nameSeparator = rawName.indexOf(QLatin1Char('|'), timestampStart);
    const int previewSeparator = nameSeparator < 0 ? -1 : rawName.indexOf(QLatin1Char('|'), nameSeparator + 1);
    if (nameSeparator < 0 || previewSeparator < 0) {
        return summary;
    }

    bool timestampOk = false;
    const qint64 timestamp = rawName.mid(timestampStart, nameSeparator - timestampStart).toLongLong(&timestampOk);
    summary.name = rawName.mid(nameSeparator + 1, previewSeparator - nameSeparator - 1).trimmed();
    summary.preview = rawName.mid(previewSeparator + 1).trimmed();
    if (timestampOk) {
        summary.lastTimestamp = timestamp;
    }

    return summary;
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

    if (m_contactsRefreshInFlight) {
        setStatusMessage(QStringLiteral("Conversation refresh is already running."));
        return;
    }

    ensureModuleEnabled();
    m_contactsRefreshInFlight = true;
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

    const int equivalentIndex = findEquivalentContactIndex(key);
    const QString selectedKey = equivalentIndex >= 0 ? m_contacts.at(equivalentIndex).key : key;

    m_selectedContactKey = selectedKey;
    if (!contactName.trimmed().isEmpty()) {
        m_selectedContactName = contactName.trimmed();
    } else {
        const ContactState* contact = findContact(selectedKey);
        m_selectedContactName = contact == nullptr ? phoneNumber : contact->name;
    }

    if (ContactState* contact = findContact(selectedKey)) {
        contact->unread = 0;
    }

    emitConversationChanged();
    emit contactsChanged();
    requestSelectedConversationMessages();
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
            m_requestedMmsContentTargets.clear();
            m_retriedMmsContentTargets.clear();
            m_pendingMmsContentTargets.clear();
            m_activeMmsContentTarget.clear();
            m_mmsAttachmentCache.clear();
            m_messageFetchesInFlight.clear();
            m_contactsRefreshInFlight = false;
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
        m_requestedMmsContentTargets.clear();
        m_retriedMmsContentTargets.clear();
        m_pendingMmsContentTargets.clear();
        m_activeMmsContentTarget.clear();
        m_mmsAttachmentCache.clear();
        m_messageFetchesInFlight.clear();
        m_contactsRefreshInFlight = false;
        setReady(false);
        setBusy(false);
        setStatusMessage(QStringLiteral("Phone disconnected."));
        return true;
    }

    if (event->type() == FetchContactListResultEvent::Type) {
        const auto* contactsEvent = static_cast<FetchContactListResultEvent*>(event);

        for (const auto& entry : contactsEvent->GetContacts()) {
            const ContactSummary summary = parseContactSummary(QString::fromStdString(entry.first));
            const QString name = summary.name;
            const QString number = QString::fromStdString(entry.second).trimmed();
            const QString key = normalizeNumber(number);
            if (key.isEmpty()) {
                continue;
            }

            ensureContactExists(key, name, number);
            const int contactIndex = findEquivalentContactIndex(key);
            ContactState* contact = contactIndex < 0 ? findContact(key) : &m_contacts[contactIndex];
            if (contact != nullptr) {
                if (!summary.preview.isEmpty()) {
                    contact->preview = buildPreview(summary.preview);
                }
                if (summary.lastTimestamp > 0) {
                    contact->lastTimestamp = summary.lastTimestamp;
                }
            }
        }

        sortContacts();
        if (m_selectedContactKey.isEmpty() && !m_contacts.isEmpty()) {
            m_selectedContactKey = m_contacts.front().key;
            m_selectedContactName = m_contacts.front().name;
            emit selectedConversationChanged();
            emit canSendChanged();
        }

        m_contactsRefreshInFlight = false;
        setBusy(false);
        setStatusMessage(QStringLiteral("Conversations loaded. Fetching selected thread..."));
        emit contactsChanged();
        updateSelectedMessages();
        requestSelectedConversationMessages(true);
        return true;
    }

    if (event->type() == FetchMessageListResultEvent::Type) {
        const auto* messagesEvent = static_cast<FetchMessageListResultEvent*>(event);
        const QString number = QString::fromStdString(messagesEvent->GetNumber()).trimmed();
        const QString key = normalizeNumber(number);
        if (key.isEmpty()) {
            return true;
        }

        const int equivalentIndex = findEquivalentContactIndex(key);
        const QString conversationKey = equivalentIndex >= 0 ? m_contacts.at(equivalentIndex).key : key;
        m_messageFetchesInFlight.remove(conversationKey);

        // Message fetch payloads only carry the number; keep any existing contact name.
        ensureContactExists(conversationKey, QString(), number);
        QVector<MessageState> parsed;
        const auto& rawMessages = messagesEvent->GetMessages();
        parsed.reserve(static_cast<qsizetype>(rawMessages.size()));
        for (int index = static_cast<int>(rawMessages.size()) - 1; index >= 0; --index) {
            parsed.push_back(parseMessage(
                QString::fromStdString(rawMessages.at(static_cast<size_t>(index))),
                conversationKey,
                index,
                ++m_timestampCounter
            ));
        }
        applyCachedMmsContent(parsed);
        requestMmsContentFetches(parsed);

        m_messagesByContact.insert(conversationKey, parsed);

        if (ContactState* contact = findContact(conversationKey)) {
            contact->loading = false;
            if (!parsed.isEmpty()) {
                const MessageState& latestMessage = parsed.back();
                contact->preview = latestMessage.body.simplified().isEmpty() && !latestMessage.attachments.isEmpty()
                    ? QStringLiteral("MMS attachment")
                    : buildPreview(latestMessage.body);
                contact->lastTimestamp = latestMessage.timestamp;
            } else if (contact->preview.isEmpty()) {
                contact->preview = QStringLiteral("No messages yet");
            }

            if (m_selectedContactKey == conversationKey) {
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

    if (event->type() == MMSContentReceivedEvent::Type) {
        const auto* mmsContentEvent = static_cast<MMSContentReceivedEvent*>(event);
        const QString target = QString::fromStdString(mmsContentEvent->GetTarget());
        const std::filesystem::path destination = mmsContentEvent->GetDestination();
        const QString fileUrl = pathToLocalFileUrl(destination);
        const QString filePath = fileUrl.isEmpty() ? QString() : QUrl(fileUrl).toLocalFile();
        const bool hasCachedAttachment = m_mmsAttachmentCache.contains(target);
        const bool shouldRetry = fileUrl.isEmpty() && !target.isEmpty() && !m_retriedMmsContentTargets.contains(target);
        if (m_activeMmsContentTarget == target) {
            m_activeMmsContentTarget.clear();
        }
        if (fileUrl.isEmpty()) {
            m_requestedMmsContentTargets.remove(target);
            if (!hasCachedAttachment && shouldRetry) {
                m_retriedMmsContentTargets.insert(target);
                queueMmsContentFetch(target);
            } else if (!hasCachedAttachment) {
                m_mmsAttachmentCache.remove(target);
            }
        } else {
            m_requestedMmsContentTargets.remove(target);
            m_retriedMmsContentTargets.remove(target);
            MessageState::AttachmentState cachedAttachment;
            cachedAttachment.target = target;
            cachedAttachment.loading = false;
            cachedAttachment.failed = false;
            cachedAttachment.filePath = filePath;
            cachedAttachment.fileUrl = fileUrl;
            cachedAttachment.previewable = isPreviewableImagePath(filePath);
            m_mmsAttachmentCache.insert(target, cachedAttachment);
        }

        bool changed = false;
        for (auto conversationIt = m_messagesByContact.begin(); conversationIt != m_messagesByContact.end(); ++conversationIt) {
            for (MessageState& message : conversationIt.value()) {
                for (MessageState::AttachmentState& attachment : message.attachments) {
                    if (attachment.target != target) {
                        continue;
                    }

                    if (fileUrl.isEmpty()) {
                        if (hasCachedAttachment) {
                            const MessageState::AttachmentState& cachedAttachment = m_mmsAttachmentCache[target];
                            attachment.loading = false;
                            attachment.failed = false;
                            attachment.filePath = cachedAttachment.filePath;
                            attachment.fileUrl = cachedAttachment.fileUrl;
                            attachment.previewable = cachedAttachment.previewable;
                            changed = true;
                            continue;
                        }

                        if (shouldRetry) {
                            attachment.loading = shouldRetry;
                            attachment.failed = false;
                            changed = true;
                            continue;
                        }

                        attachment.loading = false;
                        attachment.failed = true;
                        attachment.filePath = QString();
                        attachment.fileUrl = QString();
                        attachment.previewable = false;
                        changed = true;
                        continue;
                    }

                    attachment.loading = false;
                    attachment.failed = false;
                    attachment.filePath = filePath;
                    attachment.fileUrl = fileUrl;
                    attachment.previewable = isPreviewableImagePath(filePath);
                    changed = true;
                }
            }
        }

        if (changed) {
            updateSelectedMessages();
        }
        startNextMmsContentFetch();
        return true;
    }

    return QObject::event(event);
}

void SmsBridgeController::requestMmsContentFetches(const QVector<MessageState>& messages)
{
    for (const MessageState& message : messages) {
        for (const MessageState::AttachmentState& attachment : message.attachments) {
            if (attachment.target.isEmpty() ||
                !attachment.loading ||
                !attachment.fileUrl.isEmpty() ||
                m_requestedMmsContentTargets.contains(attachment.target)) {
                continue;
            }

            queueMmsContentFetch(attachment.target);
        }
    }
}

void SmsBridgeController::queueMmsContentFetch(const QString& target)
{
    if (target.isEmpty() || m_requestedMmsContentTargets.contains(target)) {
        return;
    }

    m_requestedMmsContentTargets.insert(target);
    m_pendingMmsContentTargets.push_back(target);
    startNextMmsContentFetch();
}

void SmsBridgeController::startNextMmsContentFetch()
{
    if (!m_activeMmsContentTarget.isEmpty() || m_pendingMmsContentTargets.isEmpty()) {
        return;
    }

    m_activeMmsContentTarget = m_pendingMmsContentTargets.takeFirst();
    auto& module = ModulesManager::GetModuleReference<SmsBridgeModule>();
    module->FetchMMSContent(m_activeMmsContentTarget.toStdString());
}

void SmsBridgeController::applyCachedMmsContent(QVector<MessageState>& messages) const
{
    for (MessageState& message : messages) {
        for (MessageState::AttachmentState& attachment : message.attachments) {
            const auto cachedIt = m_mmsAttachmentCache.constFind(attachment.target);
            if (cachedIt == m_mmsAttachmentCache.constEnd()) {
                continue;
            }

            const MessageState::AttachmentState& cachedAttachment = cachedIt.value();
            attachment.filePath = cachedAttachment.filePath;
            attachment.fileUrl = cachedAttachment.fileUrl;
            attachment.previewable = cachedAttachment.previewable;
            attachment.loading = cachedAttachment.loading;
            attachment.failed = cachedAttachment.failed;
        }
    }
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

QString SmsBridgeController::digitsOnly(const QString& number)
{
    QString result;
    result.reserve(number.size());
    for (const QChar c : number) {
        if (c.isDigit()) {
            result.append(c);
        }
    }
    return result;
}

bool SmsBridgeController::areEquivalentPhoneKeys(const QString& lhs, const QString& rhs)
{
    if (lhs == rhs) {
        return true;
    }

    const QString lhsDigits = digitsOnly(lhs);
    const QString rhsDigits = digitsOnly(rhs);
    if (lhsDigits.isEmpty() || rhsDigits.isEmpty()) {
        return false;
    }

    const qsizetype shorterLength = std::min(lhsDigits.size(), rhsDigits.size());
    if (shorterLength < 7) {
        return false;
    }

    return lhsDigits.endsWith(rhsDigits) || rhsDigits.endsWith(lhsDigits);
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

    const QStringList attachmentTargets = extractMmsAttachmentTargets(rawMessage);
    parsed.attachments.reserve(attachmentTargets.size());
    for (const QString& target : attachmentTargets) {
        MessageState::AttachmentState attachment;
        attachment.target = target;
        attachment.loading = true;
        attachment.failed = false;
        parsed.attachments.push_back(attachment);
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

QString SmsBridgeController::pathToLocalFileUrl(const std::filesystem::path& path)
{
    if (path.empty()) {
        return QString();
    }

#ifdef _WIN32
    const QString localPath = QString::fromStdWString(path.wstring());
#else
    const QString localPath = QString::fromStdString(path.string());
#endif
    return QUrl::fromLocalFile(localPath).toString();
}

bool SmsBridgeController::isPreviewableImagePath(const QString& path)
{
    if (path.isEmpty()) {
        return false;
    }

    const QString lower = path.toLower();
    const bool imageExtension = lower.endsWith(QStringLiteral(".jpg")) ||
                                lower.endsWith(QStringLiteral(".jpeg")) ||
                                lower.endsWith(QStringLiteral(".png")) ||
                                lower.endsWith(QStringLiteral(".gif")) ||
                                lower.endsWith(QStringLiteral(".bmp")) ||
                                lower.endsWith(QStringLiteral(".webp"));
    if (!imageExtension) {
        return false;
    }

    QImageReader reader(path);
    return reader.canRead();
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
    int existingIndex = findContactIndex(key);
    if (existingIndex < 0) {
        existingIndex = findEquivalentContactIndex(key);
    }

    ContactState* existing = existingIndex < 0 ? nullptr : &m_contacts[existingIndex];
    if (existing != nullptr) {
        const QString trimmedDisplayName = displayName.trimmed();
        const QString trimmedDialNumber = dialNumber.trimmed();
        const bool displayNameLooksLikeNumber = areEquivalentPhoneKeys(trimmedDisplayName, key);
        const bool existingNameLooksLikeNumber = areEquivalentPhoneKeys(existing->name, existing->key);

        if (!trimmedDisplayName.isEmpty() && (!displayNameLooksLikeNumber || existingNameLooksLikeNumber)) {
            existing->name = trimmedDisplayName;
        } else if (existing->name.isEmpty()) {
            existing->name = dialNumber;
        }

        if (!trimmedDialNumber.isEmpty() &&
            (existing->number.isEmpty() ||
             (!existing->number.startsWith(QLatin1Char('+')) && trimmedDialNumber.startsWith(QLatin1Char('+'))))) {
            existing->number = trimmedDialNumber;
        }

        if (m_selectedContactKey == key && existing->key != key) {
            m_selectedContactKey = existing->key;
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

void SmsBridgeController::requestSelectedConversationMessages(const bool forceRefresh)
{
    if (!m_connected || m_selectedContactKey.isEmpty()) {
        return;
    }

    if (!forceRefresh && m_messagesByContact.contains(m_selectedContactKey)) {
        return;
    }

    if (m_messageFetchesInFlight.contains(m_selectedContactKey)) {
        return;
    }

    ContactState* selected = findContact(m_selectedContactKey);
    if (selected == nullptr || selected->number.isEmpty()) {
        return;
    }

    m_messageFetchesInFlight.insert(m_selectedContactKey);
    selected->loading = true;
    emit contactsChanged();

    auto& module = ModulesManager::GetModuleReference<SmsBridgeModule>();
    module->GetTargetMessages(selected->number.toStdString());
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

int SmsBridgeController::findEquivalentContactIndex(const QString& key) const
{
    for (int i = 0; i < m_contacts.size(); ++i) {
        if (areEquivalentPhoneKeys(m_contacts.at(i).key, key)) {
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
        QVariantList attachments;
        attachments.reserve(message.attachments.size());
        for (const MessageState::AttachmentState& attachment : message.attachments) {
            QVariantMap attachmentMap;
            attachmentMap.insert(QStringLiteral("target"), attachment.target);
            attachmentMap.insert(QStringLiteral("filePath"), attachment.filePath);
            attachmentMap.insert(QStringLiteral("fileUrl"), attachment.fileUrl);
            attachmentMap.insert(QStringLiteral("previewable"), attachment.previewable);
            attachmentMap.insert(QStringLiteral("loading"), attachment.loading);
            attachmentMap.insert(QStringLiteral("failed"), attachment.failed);
            attachments.push_back(attachmentMap);
        }
        map.insert(QStringLiteral("attachments"), attachments);
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
