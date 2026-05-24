#include "MediaNotificationController.h"

#include <ConnectionManager.h>
#include <Events.h>
#include <MediaNotificationManager.h>
#include <RemoteInputEvents.h>
#include <InputTypes.h>
#include <QPointer>
#include <DebugLog.h>

MediaNotificationController::MediaNotificationController(QObject* parent)
    : QObject(parent)
    , m_settings(QStringLiteral("LibreConnect"), QStringLiteral("LibreConnect"))
{
    m_enabled = m_settings.value(QStringLiteral("mediaNotification/enabled"), true).toBool();
    Debug::Log("Desktop MediaNotificationController created. m_enabled={}", m_enabled);
    ConnectionManager::AddEventListener(QPointer<QObject>(this));

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

    emit trackInfoChanged();
    updateNativeNotification();
}

bool MediaNotificationController::event(QEvent* event)
{
    if (event->type() == ConnectedEvent::Type) {
        Debug::Log("Desktop MediaNotificationController: ConnectedEvent received. Resetting state.");
        m_trackTitle.clear();
        m_trackArtist.clear();
        m_trackAlbum.clear();
        m_duration = 0.0;
        m_position = 0.0;
        m_playing = false;
        emit trackInfoChanged();
        updateNativeNotification();
        return true;
    }

    if (event->type() == DisconnectedEvent::Type) {
        Debug::Log("Desktop MediaNotificationController: DisconnectedEvent received. Resetting state.");
        m_trackTitle.clear();
        m_trackArtist.clear();
        m_trackAlbum.clear();
        m_duration = 0.0;
        m_position = 0.0;
        m_playing = false;
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

        MediaNotificationManager::UpdateMetadata(metadata);
        MediaNotificationManager::UpdatePlaybackState(m_playing, m_position);
    }
}
