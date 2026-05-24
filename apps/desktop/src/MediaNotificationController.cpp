#include "MediaNotificationController.h"

#include <ConnectionManager.h>
#include <Events.h>
#include <MediaNotificationManager.h>
#include <RemoteInputEvents.h>
#include <InputTypes.h>
#include <QPointer>
#include <DebugLog.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace
{
constexpr int kMinMediaSignal = static_cast<int>(MediaSignal::PlayPause);
constexpr int kMaxMediaSignal = static_cast<int>(MediaSignal::VolumeMute);
}

MediaNotificationController::MediaNotificationController(QObject* parent)
    : QObject(parent)
    , m_settings(QStringLiteral("LibreConnect"), QStringLiteral("LibreConnect"))
{
    m_enabled = m_settings.value(QStringLiteral("mediaNotification/enabled"), true).toBool();
    Debug::Log("Desktop MediaNotificationController created. m_enabled={}", m_enabled);
    ConnectionManager::AddEventListener(QPointer<QObject>(this));
    m_progressTimer.setInterval(1000);
    connect(&m_progressTimer, &QTimer::timeout, this, &MediaNotificationController::onProgressTick);

    if (m_enabled) {
        MediaNotificationManager::Show();
        MediaNotificationManager::SetActionCallback([](MediaSignal signal) {
            Debug::Log("Desktop MediaNotificationController: ActionCallback triggered from native notification. signal={}", static_cast<int>(signal));
            ConnectionManager::Send(PC_PackageType::REMOTE_INPUT_MODULE_SEND_MEDIA_INPUT, signal);
        });
        MediaNotificationManager::SetSeekCallback([](double posSeconds) {
            Debug::Log("Desktop MediaNotificationController: SeekCallback triggered from native notification. position={:.2f}s", posSeconds);
            ConnectionManager::Send(PC_PackageType::REMOTE_INPUT_MODULE_SET_MEDIA_POSITION, posSeconds);
        });
    }
}

MediaNotificationController::~MediaNotificationController()
{
    Debug::Log("Desktop MediaNotificationController destroyed.");
    if (m_enabled) {
        MediaNotificationManager::Hide();
        MediaNotificationManager::SetActionCallback(nullptr);
        MediaNotificationManager::SetSeekCallback(nullptr);
    }
}

void MediaNotificationController::setEnabled(bool enabled)
{
    Debug::Log("Desktop MediaNotificationController::setEnabled({}) called", enabled);
    if (m_enabled == enabled)
        return;

    m_enabled = enabled;
    m_settings.setValue(QStringLiteral("mediaNotification/enabled"), m_enabled);
    emit enabledChanged();

    if (m_enabled) {
        MediaNotificationManager::Show();
        MediaNotificationManager::SetActionCallback([](MediaSignal signal) {
            Debug::Log("Desktop MediaNotificationController: ActionCallback triggered from native notification. signal={}", static_cast<int>(signal));
            ConnectionManager::Send(PC_PackageType::REMOTE_INPUT_MODULE_SEND_MEDIA_INPUT, signal);
        });
        MediaNotificationManager::SetSeekCallback([](double posSeconds) {
            Debug::Log("Desktop MediaNotificationController: SeekCallback triggered from native notification. position={:.2f}s", posSeconds);
            ConnectionManager::Send(PC_PackageType::REMOTE_INPUT_MODULE_SET_MEDIA_POSITION, posSeconds);
        });
        updateNativeNotification();
    } else {
        MediaNotificationManager::Hide();
        MediaNotificationManager::SetActionCallback(nullptr);
        MediaNotificationManager::SetSeekCallback(nullptr);
    }
}

void MediaNotificationController::show()
{
    setEnabled(true);
}

void MediaNotificationController::hide()
{
    setEnabled(false);
}

void MediaNotificationController::setTrackInfo(const QString& title, const QString& artist, const QString& album, double duration, double position, bool playing)
{
    m_trackTitle = title;
    m_trackArtist = artist;
    m_trackAlbum = album;
    m_duration = duration;
    m_position = position;
    m_playing = playing;
    m_elapsedTime = formatTime(position);
    m_durationTime = formatTime(duration);

    restartProgressTimer();
    emit trackInfoChanged();
    updateNativeNotification();
}

void MediaNotificationController::sendMediaSignal(const int signal)
{
    if (signal < kMinMediaSignal || signal > kMaxMediaSignal) {
        Debug::LogWarning("Desktop MediaNotificationController::sendMediaSignal ignored invalid signal={}", signal);
        return;
    }

    ConnectionManager::Send(
        PC_PackageType::REMOTE_INPUT_MODULE_SEND_MEDIA_INPUT,
        static_cast<MediaSignal>(signal));

    const MediaSignal mediaSignal = static_cast<MediaSignal>(signal);
    if (mediaSignal == MediaSignal::PlayPause) {
        m_playing = !m_playing;
        restartProgressTimer();
        emit trackInfoChanged();
        updateNativeNotification();
    } else if (mediaSignal == MediaSignal::VolumeDown) {
        m_volume = std::clamp(m_volume - 5, 0, 100);
        emit trackInfoChanged();
    } else if (mediaSignal == MediaSignal::VolumeUp) {
        m_volume = std::clamp(m_volume + 5, 0, 100);
        emit trackInfoChanged();
    } else if (mediaSignal == MediaSignal::VolumeMute) {
        m_volume = 0;
        emit trackInfoChanged();
    }
}

void MediaNotificationController::seekTo(const double seconds)
{
    const double clampedSeconds = std::max(0.0, std::min(seconds, std::max(0.0, m_duration)));
    m_position = clampedSeconds;
    m_elapsedTime = formatTime(clampedSeconds);
    restartProgressTimer();
    emit trackInfoChanged();
    updateNativeNotification();

    ConnectionManager::Send(
        PC_PackageType::REMOTE_INPUT_MODULE_SET_MEDIA_POSITION,
        clampedSeconds);
}

void MediaNotificationController::setVolume(const int volume)
{
    m_volume = std::clamp(volume, 0, 100);
    emit trackInfoChanged();

    ConnectionManager::Send(
        PC_PackageType::REMOTE_INPUT_MODULE_SET_VOLUME,
        m_volume);
}

bool MediaNotificationController::hasTrackInfo() const
{
    return !m_trackTitle.isEmpty() || !m_trackArtist.isEmpty() || !m_trackAlbum.isEmpty();
}

bool MediaNotificationController::event(QEvent* event)
{
    if (event->type() == ConnectedEvent::Type) {
        Debug::Log("Desktop MediaNotificationController: ConnectedEvent received. Resetting state.");
        resetTrackInfo();
        restartProgressTimer();
        emit trackInfoChanged();
        updateNativeNotification();
        return true;
    }

    if (event->type() == DisconnectedEvent::Type) {
        Debug::Log("Desktop MediaNotificationController: DisconnectedEvent received. Resetting state.");
        resetTrackInfo();
        restartProgressTimer();
        emit trackInfoChanged();
        updateNativeNotification();
        return true;
    }

    if (event->type() == RemoteMediaInfoEvent::Type) {
        const auto* mediaEvent = static_cast<RemoteMediaInfoEvent*>(event);
        m_trackTitle = QString::fromStdString(mediaEvent->GetTitle());
        m_trackArtist = QString::fromStdString(mediaEvent->GetArtist());
        m_trackAlbum = QString::fromStdString(mediaEvent->GetCollection());
        m_playing = mediaEvent->IsPlaying();
        m_position = mediaEvent->GetPositionSeconds();
        m_duration = mediaEvent->GetDurationSeconds();
        m_elapsedTime = QString::fromStdString(mediaEvent->GetElapsed());
        if (m_elapsedTime.isEmpty()) {
            m_elapsedTime = formatTime(m_position);
        }
        m_durationTime = formatTime(m_duration);
        m_volume = std::clamp(mediaEvent->GetVolume(), 0, 100);
        setCoverBytes(mediaEvent->GetCoverBytes());
        restartProgressTimer();

        Debug::Log("Desktop MediaNotificationController: RemoteMediaInfoEvent received: title='{}', artist='{}', playing={}, duration={:.2f}s, m_enabled={}",
                   mediaEvent->GetTitle(), mediaEvent->GetArtist(), mediaEvent->IsPlaying(), mediaEvent->GetDurationSeconds(), m_enabled);

        emit trackInfoChanged();

        if (m_enabled) {
            TrackMetadata metadata;
            metadata.title = mediaEvent->GetTitle();
            metadata.artist = mediaEvent->GetArtist();
            metadata.album = mediaEvent->GetCollection();
            metadata.playing = mediaEvent->IsPlaying();
            metadata.position = mediaEvent->GetPositionSeconds();
            metadata.duration = mediaEvent->GetDurationSeconds();
            metadata.cover = mediaEvent->GetCoverBytes();

            if (metadata.title.empty() && metadata.artist.empty()) {
                Debug::Log("Desktop MediaNotificationController: Empty title and artist. Hiding native notification.");
                MediaNotificationManager::Hide();
            } else {
                Debug::Log("Desktop MediaNotificationController: Updating native notification metadata and playback state.");
                MediaNotificationManager::Show();
                MediaNotificationManager::UpdateMetadata(metadata);
                MediaNotificationManager::UpdatePlaybackState(metadata.playing, metadata.position);
            }
        }
        return true;
    }

    return QObject::event(event);
}

void MediaNotificationController::resetTrackInfo()
{
    m_trackTitle.clear();
    m_trackArtist.clear();
    m_trackAlbum.clear();
    m_elapsedTime.clear();
    m_durationTime.clear();
    m_duration = 0.0;
    m_position = 0.0;
    m_playing = false;
    m_coverBytes.clear();
    m_coverImageSource.clear();
    m_volume = 0;
}

void MediaNotificationController::restartProgressTimer()
{
    m_progressBasePosition = m_position;
    m_progressElapsed.restart();

    if (m_playing && m_duration > 0.0 && m_position < m_duration) {
        if (!m_progressTimer.isActive()) {
            m_progressTimer.start();
        }
        return;
    }

    m_progressTimer.stop();
}

void MediaNotificationController::onProgressTick()
{
    if (!m_playing || m_duration <= 0.0) {
        m_progressTimer.stop();
        return;
    }

    const double nextPosition = std::min(
        m_duration,
        m_progressBasePosition + static_cast<double>(m_progressElapsed.elapsed()) / 1000.0);

    if (std::abs(nextPosition - m_position) < 0.1) {
        return;
    }

    m_position = nextPosition;
    m_elapsedTime = formatTime(m_position);
    if (m_position >= m_duration) {
        m_progressTimer.stop();
    }
    emit trackInfoChanged();
}

void MediaNotificationController::setCoverBytes(const std::vector<uint8_t>& coverBytes)
{
    if (coverBytes.empty()) {
        m_coverBytes.clear();
        m_coverImageSource.clear();
        return;
    }

    m_coverBytes = QByteArray(
        reinterpret_cast<const char*>(coverBytes.data()),
        static_cast<qsizetype>(coverBytes.size()));

    const QString mimeType = coverMimeType(m_coverBytes);
    m_coverImageSource = QStringLiteral("data:%1;base64,%2")
        .arg(mimeType)
        .arg(QString::fromLatin1(m_coverBytes.toBase64()));
}

QString MediaNotificationController::formatTime(const double seconds)
{
    if (seconds <= 0.0 || !std::isfinite(seconds)) {
        return QStringLiteral("0:00");
    }

    const long long safeSeconds = std::max(0LL, static_cast<long long>(std::floor(seconds)));
    const long long hours = safeSeconds / 3600;
    const long long minutes = (safeSeconds % 3600) / 60;
    const long long remainingSeconds = safeSeconds % 60;

    std::ostringstream stream;
    stream << std::setfill('0');
    if (hours > 0) {
        stream << hours << ':' << std::setw(2) << minutes << ':' << std::setw(2) << remainingSeconds;
    } else {
        stream << minutes << ':' << std::setw(2) << remainingSeconds;
    }

    return QString::fromStdString(stream.str());
}

QString MediaNotificationController::coverMimeType(const QByteArray& coverBytes)
{
    if (coverBytes.size() >= 3 &&
        static_cast<unsigned char>(coverBytes[0]) == 0xff &&
        static_cast<unsigned char>(coverBytes[1]) == 0xd8 &&
        static_cast<unsigned char>(coverBytes[2]) == 0xff) {
        return QStringLiteral("image/jpeg");
    }

    if (coverBytes.size() >= 12 &&
        coverBytes.startsWith("RIFF") &&
        coverBytes.mid(8, 4) == QByteArrayLiteral("WEBP")) {
        return QStringLiteral("image/webp");
    }

    return QStringLiteral("image/png");
}

void MediaNotificationController::updateNativeNotification()
{
    Debug::Log("Desktop MediaNotificationController::updateNativeNotification() called. m_enabled={}, title='{}', artist='{}'",
               m_enabled, m_trackTitle.toStdString(), m_trackArtist.toStdString());
    if (!m_enabled)
        return;

    if (m_trackTitle.isEmpty() && m_trackArtist.isEmpty()) {
        Debug::Log("Desktop MediaNotificationController: Title and artist are empty. Hiding native notification.");
        MediaNotificationManager::Hide();
    } else {
        Debug::Log("Desktop MediaNotificationController: Showing/updating native notification manually.");
        MediaNotificationManager::Show();
        TrackMetadata metadata;
        metadata.title = m_trackTitle.toStdString();
        metadata.artist = m_trackArtist.toStdString();
        metadata.album = m_trackAlbum.toStdString();
        metadata.playing = m_playing;
        metadata.position = m_position;
        metadata.duration = m_duration;
        metadata.cover.assign(m_coverBytes.begin(), m_coverBytes.end());

        MediaNotificationManager::UpdateMetadata(metadata);
        MediaNotificationManager::UpdatePlaybackState(m_playing, m_position);
    }
}
