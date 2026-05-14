#include "MobileRemoteInputController.h"

#include <algorithm>

#include <QBuffer>
#include <QDateTime>
#include <QPointer>
#include <QChar>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QStandardPaths>
#include <Qt>
#include <QUrl>

#include <ConnectionManager.h>
#include <Events.h>
#include <ModulesManager.h>
#include <RemoteInputModule.h>
#include <RemoteInputEvents.h>

#ifdef ANDROID_DEVICE
#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>
#endif

namespace {
constexpr int INVALID_KEY = -1;
QString AccessibilityPermissionMessage()
{
    return QStringLiteral(
        "Desktop accessibility permission is required for remote input. "
        "Allow LibreConnect in macOS System Settings > Privacy & Security > Accessibility."
    );
}

QString MimeTypeFromImageBytes(const QByteArray& bytes)
{
    if (bytes.isEmpty()) {
        return QString();
    }

    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return QStringLiteral("image/jpeg");
    }

    const QByteArray format = QImageReader::imageFormat(&buffer).toLower();
    if (format.isEmpty()) {
        return QStringLiteral("image/jpeg");
    }

    if (format == "jpg") {
        return QStringLiteral("image/jpeg");
    }

    return QStringLiteral("image/%1").arg(QString::fromLatin1(format));
}

QString ImageExtensionFromBytes(const QByteArray& bytes)
{
    if (bytes.isEmpty()) {
        return QStringLiteral("jpg");
    }

    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return QStringLiteral("jpg");
    }

    const QByteArray format = QImageReader::imageFormat(&buffer).toLower();
    if (format.isEmpty()) {
        return QStringLiteral("jpg");
    }

    if (format == "jpeg" || format == "jpg") {
        return QStringLiteral("jpg");
    }

    return QString::fromLatin1(format);
}

QString BuildCoverImageSource(const QByteArray& coverBytes)
{
    if (coverBytes.isEmpty()) {
        return QString();
    }

    const QString tempRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (!tempRoot.isEmpty()) {
        QDir tempDir(tempRoot);
        if (tempDir.exists() || tempDir.mkpath(QStringLiteral("."))) {
            const QString extension = ImageExtensionFromBytes(coverBytes);
            const QString filePath = tempDir.filePath(QStringLiteral("libreconnect_remote_cover.%1").arg(extension));

            QFile file(filePath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(coverBytes) == coverBytes.size()) {
                file.close();
                return QStringLiteral("%1?rev=%2")
                    .arg(
                        QUrl::fromLocalFile(filePath).toString(),
                        QString::number(QDateTime::currentMSecsSinceEpoch())
                    );
            }
        }
    }

    const QString mimeType = MimeTypeFromImageBytes(coverBytes);
    return QStringLiteral("data:%1;base64,%2").arg(mimeType, QString::fromLatin1(coverBytes.toBase64()));
}
}

MobileRemoteInputController::MobileRemoteInputController(QObject* parent)
    : QObject(parent)
{
    ConnectionManager::AddEventListener(QPointer<QObject>(this));

    m_pollTimer.setInterval(350);
    connect(&m_pollTimer, &QTimer::timeout, this, &MobileRemoteInputController::refreshState);
    m_pollTimer.start();

    m_mediaInfoTimer.setInterval(1800);
    connect(&m_mediaInfoTimer, &QTimer::timeout, this, &MobileRemoteInputController::requestNowPlayingUpdate);
    m_mediaInfoTimer.start();

    refreshState();
}

bool MobileRemoteInputController::hasTrackInfo() const
{
    return !m_trackTitle.isEmpty() || !m_trackArtist.isEmpty() || !m_trackCollection.isEmpty() || !m_coverImageSource.isEmpty();
}

void MobileRemoteInputController::setSessionActive(const bool active)
{
    if (m_sessionActive == active) {
        return;
    }

    m_sessionActive = active;
    if (m_sessionActive) {
        updateAndroidMediaNotification();
        requestNowPlayingUpdate();
    } else {
        hideAndroidMediaNotification();
    }
}

void MobileRemoteInputController::sendMediaSignal(const int signal)
{
    if (!m_connected) {
        setStatusMessage(QStringLiteral("Connect to a desktop device to use remote input."));
        return;
    }

    if (!m_ready) {
        setStatusMessage(QStringLiteral("Remote input is starting. Try again in a moment."));
        auto& module = ModulesManager::GetModuleReference<RemoteInputModule>();
        module->Enable(true);
        return;
    }

    if (!m_accessibilityGranted) {
        const QString message = AccessibilityPermissionMessage();
        setStatusMessage(QStringLiteral("Desktop accessibility permission is required for remote input."));
        emit accessibilityPermissionRequired(message);
        return;
    }

    if (signal < static_cast<int>(MediaSignal::PlayPause) ||
        signal > static_cast<int>(MediaSignal::VolumeMute)) {
        return;
    }

    RemoteInputModule::SendMediaInput(static_cast<MediaSignal>(signal));
    if (static_cast<MediaSignal>(signal) == MediaSignal::PlayPause) {
        m_playing = !m_playing;
        emit playbackChanged();
    }

    setStatusMessage(QStringLiteral("Media command sent to desktop."));
    requestNowPlayingUpdate();
    updateAndroidMediaNotification();
}

void MobileRemoteInputController::seekTo(const double seconds)
{
    if (!m_connected || !m_ready || m_durationSeconds <= 0.0) {
        return;
    }

    const double clampedSeconds = std::clamp(seconds, 0.0, m_durationSeconds);
    RemoteInputModule::SetMediaPosition(clampedSeconds);
}

void MobileRemoteInputController::sendQtKeyEvent(const int qtKey, const QString& text, const int modifiers)
{
    if (!m_connected) {
        setStatusMessage(QStringLiteral("Connect to a desktop device to use remote keyboard."));
        return;
    }

    if (!m_ready) {
        setStatusMessage(QStringLiteral("Remote input is starting. Try again in a moment."));
        auto& module = ModulesManager::GetModuleReference<RemoteInputModule>();
        module->Enable(true);
        return;
    }

    if (!m_accessibilityGranted) {
        const QString message = AccessibilityPermissionMessage();
        setStatusMessage(QStringLiteral("Desktop accessibility permission is required for remote input."));
        emit accessibilityPermissionRequired(message);
        return;
    }

    KeyMapping special = mapQtSpecialKey(qtKey);
    if (special.key != INVALID_KEY) {
        const bool shifted = special.requiresShift || (modifiers & Qt::ShiftModifier);
        if (sendMappedKey(special.key, shifted)) {
            setStatusMessage(QStringLiteral("Keyboard input sent to desktop."));
        }
        return;
    }

    if (text.isEmpty()) {
        return;
    }

    bool anySent = false;
    for (const QChar c : text) {
        if (sendCharacter(c)) {
            anySent = true;
        }
    }

    if (anySent) {
        setStatusMessage(QStringLiteral("Keyboard input sent to desktop."));
    }
}

void MobileRemoteInputController::presenterPreviousSlide()
{
    sendQtKeyEvent(Qt::Key_PageUp, QString(), 0);
    if (m_connected && m_ready) {
        setStatusMessage(QStringLiteral("Previous slide command sent."));
    }
}

void MobileRemoteInputController::presenterNextSlide()
{
    sendQtKeyEvent(Qt::Key_PageDown, QString(), 0);
    if (m_connected && m_ready) {
        setStatusMessage(QStringLiteral("Next slide command sent."));
    }
}

void MobileRemoteInputController::presenterStartSlideshow()
{
    sendQtKeyEvent(Qt::Key_F5, QString(), 0);
    if (m_connected && m_ready) {
        setStatusMessage(QStringLiteral("Start slideshow command sent."));
    }
}

void MobileRemoteInputController::presenterEndSlideshow()
{
    sendQtKeyEvent(Qt::Key_Escape, QString(), 0);
    if (m_connected && m_ready) {
        setStatusMessage(QStringLiteral("End slideshow command sent."));
    }
}

void MobileRemoteInputController::setNowPlayingInfo(
    const QString& title,
    const QString& artist,
    const QString& collection,
    const QString& elapsed,
    const bool playing,
    const double positionSeconds,
    const double durationSeconds,
    const std::vector<uint8_t>& coverBytes
)
{
    const double safePosition = std::max(0.0, positionSeconds);
    const double safeDuration = std::max(0.0, durationSeconds);
    const QString normalizedElapsed = elapsed.isEmpty() ? formatTime(safePosition) : elapsed;
    const QString normalizedDuration = formatTime(safeDuration);
    QByteArray coverArray;
    if (!coverBytes.empty()) {
        coverArray = QByteArray(reinterpret_cast<const char*>(coverBytes.data()), static_cast<int>(coverBytes.size()));
    }
    const bool coverChanged = m_coverBytes != coverArray;
    const QString coverSource = !coverChanged
        ? m_coverImageSource
        : (coverArray.isEmpty()
            ? QString()
            : BuildCoverImageSource(coverArray));

    const bool changed = m_trackTitle != title ||
                         m_trackArtist != artist ||
                         m_trackCollection != collection ||
                         m_elapsedTime != normalizedElapsed ||
                         m_durationTime != normalizedDuration ||
                         m_positionSeconds != safePosition ||
                         m_durationSeconds != safeDuration ||
                         coverChanged;
    const bool playbackChangedValue = m_playing != playing;

    m_trackTitle = title;
    m_trackArtist = artist;
    m_trackCollection = collection;
    m_elapsedTime = normalizedElapsed;
    m_durationTime = normalizedDuration;
    m_positionSeconds = safePosition;
    m_durationSeconds = safeDuration;
    m_coverBytes = std::move(coverArray);
    m_coverImageSource = coverSource;
    m_playing = playing;

    if (changed) {
        emit nowPlayingChanged();
    }

    if (playbackChangedValue) {
        emit playbackChanged();
    }

    updateAndroidMediaNotification();
}

bool MobileRemoteInputController::event(QEvent* event)
{
    if (event->type() == ConnectedEvent::Type) {
        const auto* connectedEvent = static_cast<ConnectedEvent*>(event);
        const bool connectedNow = connectedEvent->GetResult() == EventResult::SUCCESS;
        if (m_connected != connectedNow) {
            m_connected = connectedNow;
            emit connectedChanged();
        }

        if (m_connected) {
            auto& module = ModulesManager::GetModuleReference<RemoteInputModule>();
            module->Enable(true);
            requestNowPlayingUpdate();
        } else {
            m_accessibilityGranted = true;
            setNowPlayingInfo(QString(), QString(), QString(), QString(), false);
            hideAndroidMediaNotification();
        }

        refreshState();
        return true;
    }

    if (event->type() == DisconnectedEvent::Type) {
        if (m_connected) {
            m_connected = false;
            emit connectedChanged();
        }

        m_accessibilityGranted = true;
        setReadyState(false);
        setStatusMessage(QStringLiteral("Connect to a desktop device to use remote input."));
        setNowPlayingInfo(QString(), QString(), QString(), QString(), false);
        hideAndroidMediaNotification();
        return true;
    }

    if (event->type() == RemoteMediaInfoEvent::Type) {
        const auto* mediaEvent = static_cast<RemoteMediaInfoEvent*>(event);
        setNowPlayingInfo(
            QString::fromStdString(mediaEvent->GetTitle()),
            QString::fromStdString(mediaEvent->GetArtist()),
            QString::fromStdString(mediaEvent->GetCollection()),
            QString::fromStdString(mediaEvent->GetElapsed()),
            mediaEvent->IsPlaying(),
            mediaEvent->GetPositionSeconds(),
            mediaEvent->GetDurationSeconds(),
            mediaEvent->GetCoverBytes()
        );
        return true;
    }

    if (event->type() == ModuleRequestedPermissionGranted::Type) {
        const auto* grantedEvent = static_cast<ModuleRequestedPermissionGranted*>(event);
        if (grantedEvent->GetPermissionType() == PermissionType::Accessibility) {
            m_accessibilityGranted = true;
            setStatusMessage(QStringLiteral("Desktop accessibility permission granted."));
        }
        return true;
    }

    if (event->type() == ModuleRequestedPermissionRejected::Type) {
        const auto* rejectedEvent = static_cast<ModuleRequestedPermissionRejected*>(event);
        if (rejectedEvent->GetPermissionType() == PermissionType::Accessibility) {
            m_accessibilityGranted = false;
            setStatusMessage(QStringLiteral("Desktop accessibility permission is required for remote input."));
            emit accessibilityPermissionRequired(AccessibilityPermissionMessage());
        }
        return true;
    }

    if (event->type() == ModuleErrorEvent::Type) {
        const auto* errorEvent = static_cast<ModuleErrorEvent*>(event);
        if (errorEvent->GetModuleType() == ModuleType::RemoteInput) {
            const QString message = QStringLiteral("%1 module error: %2.")
                                        .arg(QString::fromLatin1(ModuleTypeToString(errorEvent->GetModuleType())))
                                        .arg(QString::fromLatin1(ModuleFailReasonToString(errorEvent->GetError())));
            setStatusMessage(message);
        }
        return true;
    }

    return QObject::event(event);
}

void MobileRemoteInputController::refreshState()
{
    auto& module = ModulesManager::GetModuleReference<RemoteInputModule>();
    const ModuleState state = module->GetModuleState();

    if (!m_connected) {
        setReadyState(false);
        setStatusMessage(QStringLiteral("Connect to a desktop device to use remote input."));
        return;
    }

    if (state == ModuleState::Disabled) {
        setReadyState(false);
        setStatusMessage(QStringLiteral("Starting remote input module..."));
        module->Enable(true);
        return;
    }

    if (state == ModuleState::Disabling) {
        setReadyState(false);
        setStatusMessage(QStringLiteral("Stopping remote input module..."));
        return;
    }

    if (state == ModuleState::Enabling || state == ModuleState::Enabled) {
        setReadyState(m_accessibilityGranted);
        setStatusMessage(m_accessibilityGranted
            ? QStringLiteral("Remote input is ready.")
            : QStringLiteral("Desktop accessibility permission is required for remote input."));
        return;
    }

    setReadyState(false);
    setStatusMessage(QStringLiteral("Remote input is unavailable."));
}

void MobileRemoteInputController::requestNowPlayingUpdate()
{
    if (!m_sessionActive || !m_connected || !m_ready) {
        return;
    }

    RemoteInputModule::RequestMediaInfo();
}

bool MobileRemoteInputController::sendMappedKey(const int key, const bool requiresShift)
{
    if (key == INVALID_KEY) {
        return false;
    }

    const Key mappedKey = static_cast<Key>(key);
    if (requiresShift) {
        RemoteInputModule::SendInput(Key::LeftShift, InputEventType::PRESS);
        RemoteInputModule::SendInput(mappedKey, InputEventType::PRESS_AND_RELEASE);
        RemoteInputModule::SendInput(Key::LeftShift, InputEventType::RELEASE);
        return true;
    }

    RemoteInputModule::SendInput(mappedKey, InputEventType::PRESS_AND_RELEASE);
    return true;
}

bool MobileRemoteInputController::sendCharacter(const QChar c)
{
    const KeyMapping mapping = mapCharacter(c);
    if (mapping.key == INVALID_KEY) {
        return false;
    }

    return sendMappedKey(mapping.key, mapping.requiresShift);
}

void MobileRemoteInputController::setStatusMessage(const QString& message)
{
    if (m_statusMessage == message) {
        return;
    }

    m_statusMessage = message;
    emit statusMessageChanged();
}

void MobileRemoteInputController::setReadyState(const bool ready)
{
    if (m_ready == ready) {
        return;
    }

    m_ready = ready;
    emit readyChanged();
    if (m_ready) {
        requestNowPlayingUpdate();
    }
}

QString MobileRemoteInputController::formatTime(const double seconds)
{
    if (seconds <= 0.0) {
        return QString();
    }

    const int total = static_cast<int>(seconds);
    const int hours = total / 3600;
    const int minutes = (total % 3600) / 60;
    const int secs = total % 60;

    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QChar('0'))
            .arg(secs, 2, 10, QChar('0'));
    }

    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(secs, 2, 10, QChar('0'));
}

MobileRemoteInputController::KeyMapping MobileRemoteInputController::mapQtSpecialKey(const int qtKey)
{
    switch (qtKey) {
    case Qt::Key_Backspace:
        return { static_cast<int>(Key::Backspace), false };
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return { static_cast<int>(Key::Enter), false };
    case Qt::Key_Tab:
        return { static_cast<int>(Key::Tab), false };
    case Qt::Key_Escape:
        return { static_cast<int>(Key::Escape), false };
    case Qt::Key_Space:
        return { static_cast<int>(Key::Space), false };
    case Qt::Key_Up:
        return { static_cast<int>(Key::Up), false };
    case Qt::Key_Down:
        return { static_cast<int>(Key::Down), false };
    case Qt::Key_Left:
        return { static_cast<int>(Key::Left), false };
    case Qt::Key_Right:
        return { static_cast<int>(Key::Right), false };
    case Qt::Key_PageUp:
        return { static_cast<int>(Key::PageUp), false };
    case Qt::Key_PageDown:
        return { static_cast<int>(Key::PageDown), false };
    case Qt::Key_Home:
        return { static_cast<int>(Key::Home), false };
    case Qt::Key_End:
        return { static_cast<int>(Key::End), false };
    case Qt::Key_Delete:
        return { static_cast<int>(Key::Delete), false };
    case Qt::Key_Insert:
        return { static_cast<int>(Key::Insert), false };
    case Qt::Key_F1:
        return { static_cast<int>(Key::F1), false };
    case Qt::Key_F2:
        return { static_cast<int>(Key::F2), false };
    case Qt::Key_F3:
        return { static_cast<int>(Key::F3), false };
    case Qt::Key_F4:
        return { static_cast<int>(Key::F4), false };
    case Qt::Key_F5:
        return { static_cast<int>(Key::F5), false };
    case Qt::Key_F6:
        return { static_cast<int>(Key::F6), false };
    case Qt::Key_F7:
        return { static_cast<int>(Key::F7), false };
    case Qt::Key_F8:
        return { static_cast<int>(Key::F8), false };
    case Qt::Key_F9:
        return { static_cast<int>(Key::F9), false };
    case Qt::Key_F10:
        return { static_cast<int>(Key::F10), false };
    case Qt::Key_F11:
        return { static_cast<int>(Key::F11), false };
    case Qt::Key_F12:
        return { static_cast<int>(Key::F12), false };
    default:
        return { INVALID_KEY, false };
    }
}

MobileRemoteInputController::KeyMapping MobileRemoteInputController::mapCharacter(const QChar c)
{
    if (c.isLetter()) {
        const QChar lower = c.toLower();
        if (lower < QChar('a') || lower > QChar('z')) {
            return { INVALID_KEY, false };
        }

        const int offset = lower.unicode() - QChar('a').unicode();
        return { static_cast<int>(Key::A) + offset, c.isUpper() };
    }

    if (c.isDigit()) {
        const int offset = c.unicode() - QChar('0').unicode();
        return { static_cast<int>(Key::Num0) + offset, false };
    }

    switch (c.unicode()) {
    case ' ':
        return { static_cast<int>(Key::Space), false };
    case '-':
        return { static_cast<int>(Key::Minus), false };
    case '_':
        return { static_cast<int>(Key::Minus), true };
    case '=':
        return { static_cast<int>(Key::Equal), false };
    case '+':
        return { static_cast<int>(Key::Equal), true };
    case '[':
        return { static_cast<int>(Key::LeftBracket), false };
    case '{':
        return { static_cast<int>(Key::LeftBracket), true };
    case ']':
        return { static_cast<int>(Key::RightBracket), false };
    case '}':
        return { static_cast<int>(Key::RightBracket), true };
    case '\\':
        return { static_cast<int>(Key::Backslash), false };
    case '|':
        return { static_cast<int>(Key::Backslash), true };
    case ';':
        return { static_cast<int>(Key::Semicolon), false };
    case ':':
        return { static_cast<int>(Key::Semicolon), true };
    case '\'':
        return { static_cast<int>(Key::Apostrophe), false };
    case '"':
        return { static_cast<int>(Key::Apostrophe), true };
    case ',':
        return { static_cast<int>(Key::Comma), false };
    case '<':
        return { static_cast<int>(Key::Comma), true };
    case '.':
        return { static_cast<int>(Key::Period), false };
    case '>':
        return { static_cast<int>(Key::Period), true };
    case '/':
        return { static_cast<int>(Key::Slash), false };
    case '?':
        return { static_cast<int>(Key::Slash), true };
    case '`':
        return { static_cast<int>(Key::Grave), false };
    case '~':
        return { static_cast<int>(Key::Grave), true };
    case '!':
        return { static_cast<int>(Key::Num1), true };
    case '@':
        return { static_cast<int>(Key::Num2), true };
    case '#':
        return { static_cast<int>(Key::Num3), true };
    case '$':
        return { static_cast<int>(Key::Num4), true };
    case '%':
        return { static_cast<int>(Key::Num5), true };
    case '^':
        return { static_cast<int>(Key::Num6), true };
    case '&':
        return { static_cast<int>(Key::Num7), true };
    case '*':
        return { static_cast<int>(Key::Num8), true };
    case '(':
        return { static_cast<int>(Key::Num9), true };
    case ')':
        return { static_cast<int>(Key::Num0), true };
    case '\n':
    case '\r':
        return { static_cast<int>(Key::Enter), false };
    default:
        return { INVALID_KEY, false };
    }
}

void MobileRemoteInputController::updateAndroidMediaNotification() const
{
#ifdef ANDROID_DEVICE
    if (!m_sessionActive || !m_connected) {
        hideAndroidMediaNotification();
        return;
    }

    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        return;
    }

    const QString title = m_trackTitle.isEmpty() ? QStringLiteral("No media info") : m_trackTitle;
    const QString artist = m_trackArtist.isEmpty() ? QStringLiteral("Remote desktop") : m_trackArtist;
    const QString collection = m_trackCollection;
    const QString elapsed = m_elapsedTime;
    jbyteArray coverBytes = nullptr;

    QJniEnvironment env;
    if (!m_coverBytes.isEmpty()) {
        coverBytes = env->NewByteArray(m_coverBytes.size());
        if (coverBytes != nullptr) {
            env->SetByteArrayRegion(
                coverBytes,
                0,
                m_coverBytes.size(),
                reinterpret_cast<const jbyte*>(m_coverBytes.constData())
            );
        }
    }

    QJniObject::callStaticMethod<void>(
        "com/LibreConnect/mobile/RemoteInputNotification",
        "show",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ZDD[B)V",
        context.object<jobject>(),
        QJniObject::fromString(title).object<jstring>(),
        QJniObject::fromString(artist).object<jstring>(),
        QJniObject::fromString(collection).object<jstring>(),
        QJniObject::fromString(elapsed).object<jstring>(),
        static_cast<jboolean>(m_playing),
        static_cast<jdouble>(m_positionSeconds),
        static_cast<jdouble>(m_durationSeconds),
        coverBytes
    );

    if (coverBytes != nullptr) {
        env->DeleteLocalRef(coverBytes);
    }
#endif
}

void MobileRemoteInputController::hideAndroidMediaNotification() const
{
#ifdef ANDROID_DEVICE
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        return;
    }

    QJniObject::callStaticMethod<void>(
        "com/LibreConnect/mobile/RemoteInputNotification",
        "hide",
        "(Landroid/content/Context;)V",
        context.object<jobject>()
    );
#endif
}
