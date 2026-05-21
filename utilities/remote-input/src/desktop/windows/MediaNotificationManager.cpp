#include "MediaNotificationManager.h"
#include "MediaTrackInfo.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Media.Playback.h>
#include <winrt/Windows.Storage.Streams.h>

#include <mutex>
#include <chrono>

namespace {
    void EnsureWinRtInitialized() {
        thread_local bool initialized = false;

        if (!initialized) {
            try {
                winrt::init_apartment();
            } catch (...) {}

            initialized = true;
        }
    }

    std::mutex g_mutex;
    std::function<void(MediaSignal)> g_actionCallback;
    std::function<void(double)> g_seekCallback;

    winrt::Windows::Media::Playback::MediaPlayer g_player{ nullptr };
    winrt::Windows::Media::SystemMediaTransportControls g_smtc{ nullptr };

    winrt::event_token g_buttonToken;
    winrt::event_token g_seekToken;

    TrackMetadata g_metadata;
}

void MediaNotificationManager::Show() {
    EnsureWinRtInitialized();
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_player)
        return;

    g_player = winrt::Windows::Media::Playback::MediaPlayer();
    g_smtc = g_player.SystemMediaTransportControls();

    g_player.CommandManager().IsEnabled(false);

    g_smtc.IsPlayEnabled(true);
    g_smtc.IsPauseEnabled(true);
    g_smtc.IsNextEnabled(true);
    g_smtc.IsPreviousEnabled(true);
    g_smtc.IsStopEnabled(true);

    g_buttonToken = g_smtc.ButtonPressed([](winrt::Windows::Media::SystemMediaTransportControls const&, winrt::Windows::Media::SystemMediaTransportControlsButtonPressedEventArgs const& args) {
        auto button = args.Button();
        MediaSignal sig;

        switch (button) {
            case winrt::Windows::Media::SystemMediaTransportControlsButton::Play:

            case winrt::Windows::Media::SystemMediaTransportControlsButton::Pause:
                sig = MediaSignal::PlayPause;
            break;

            case winrt::Windows::Media::SystemMediaTransportControlsButton::Next:
                sig = MediaSignal::NextTrack;
            break;

            case winrt::Windows::Media::SystemMediaTransportControlsButton::Previous:
                sig = MediaSignal::PreviousTrack;
            break;

            default:
                return;
        }

        std::function<void(MediaSignal)> cb;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            cb = g_actionCallback;
        }

        if (cb)
            cb(sig);
    });

    g_seekToken = g_smtc.PlaybackPositionChangeRequested([](winrt::Windows::Media::SystemMediaTransportControls const&, winrt::Windows::Media::PlaybackPositionChangeRequestedEventArgs const& args) {
        double pos = static_cast<double>(args.RequestedPlaybackPosition().count()) / 10000000.0;

        std::function<void(double)> cb;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            cb = g_seekCallback;
        }

        if (cb)
            cb(pos);
    });
}

void MediaNotificationManager::Hide() {
    EnsureWinRtInitialized();
    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_player)
        return;

    g_smtc.ButtonPressed(g_buttonToken);
    g_smtc.PlaybackPositionChangeRequested(g_seekToken);

    g_smtc.IsEnabled(false);
    g_smtc = nullptr;

    g_player.Close();
    g_player = nullptr;
}

void MediaNotificationManager::UpdateMetadata(const TrackMetadata& metadata) {
    EnsureWinRtInitialized();
    std::lock_guard<std::mutex> lock(g_mutex);

    g_metadata = metadata;

    if (!g_smtc)
        return;

    auto updater = g_smtc.DisplayUpdater();
    updater.Type(winrt::Windows::Media::MediaPlaybackType::Music);

    auto musicProps = updater.MusicProperties();
    musicProps.Title(winrt::to_hstring(metadata.title));
    musicProps.Artist(winrt::to_hstring(metadata.artist));
    musicProps.AlbumTitle(winrt::to_hstring(metadata.album));

    if (!metadata.cover.empty()) {
        winrt::Windows::Storage::Streams::InMemoryRandomAccessStream stream;
        winrt::Windows::Storage::Streams::DataWriter writer(stream);

        writer.WriteBytes(winrt::array_view<const uint8_t>(metadata.cover));
        writer.StoreAsync().get();
        writer.FlushAsync().get();
        stream.Seek(0);

        auto streamRef = winrt::Windows::Storage::Streams::RandomAccessStreamReference::CreateFromStream(stream);
        updater.Thumbnail(streamRef);
    } else {
        updater.Thumbnail(nullptr);
    }

    updater.Update();
}

void MediaNotificationManager::UpdatePlaybackState(bool isPlaying, double position) {
    EnsureWinRtInitialized();
    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_smtc)
        return;

    g_smtc.PlaybackStatus(isPlaying ?
        winrt::Windows::Media::MediaPlaybackStatus::Playing :
        winrt::Windows::Media::MediaPlaybackStatus::Paused);

    winrt::Windows::Media::SystemMediaTransportControlsTimelineProperties timeline;
    timeline.StartTime(std::chrono::seconds(0));
    timeline.MinSeekTime(std::chrono::seconds(0));

    auto posTicks = std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(std::chrono::duration<double>(position));
    auto durTicks = std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(std::chrono::duration<double>(g_metadata.duration));

    timeline.Position(posTicks);
    timeline.MaxSeekTime(durTicks);
    timeline.EndTime(durTicks);

    g_smtc.UpdateTimelineProperties(timeline);
}

void MediaNotificationManager::SetActionCallback(const std::function<void(MediaSignal)>& callback) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_actionCallback = callback;
}

void MediaNotificationManager::SetSeekCallback(const std::function<void(double)>& callback) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_seekCallback = callback;
}
