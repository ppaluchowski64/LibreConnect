#include "MediaTrackInfo.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include <string>
#include <vector>
#include <optional>
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
}

std::optional<TrackMetadata> MediaTrackInfo::GetCurrentTrack() {
    EnsureWinRtInitialized();

    try {
        auto manager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        auto session = manager.GetCurrentSession();

        if (!session)
            return std::nullopt;

        auto props = session.TryGetMediaPropertiesAsync().get();
        auto timeline = session.GetTimelineProperties();
        auto playback = session.GetPlaybackInfo();

        TrackMetadata info{};
        info.title = winrt::to_string(props.Title());
        info.artist = winrt::to_string(props.Artist());
        info.album = winrt::to_string(props.AlbumTitle());

        info.duration = static_cast<double>(timeline.EndTime().count()) / 10000000.0;
        info.playing = (playback.PlaybackStatus() == winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing);

        double rawPos = static_cast<double>(timeline.Position().count()) / 10000000.0;
        int64_t lastUpdate = timeline.LastUpdatedTime().time_since_epoch().count() / 10;

        info.position = CalculateInterpolatedPosition(rawPos, lastUpdate, info.playing);

        if (info.duration > 0.0 && info.position > info.duration)
            info.position = info.duration;

        if (auto thumb = props.Thumbnail()) {
            auto stream = thumb.OpenReadAsync().get();
            auto size = static_cast<uint32_t>(stream.Size());

            if (size > 0) {
                winrt::Windows::Storage::Streams::Buffer buffer(size);
                stream.ReadAsync(buffer, size, winrt::Windows::Storage::Streams::InputStreamOptions::None).get();

                auto reader = winrt::Windows::Storage::Streams::DataReader::FromBuffer(buffer);
                info.cover.resize(size);
                reader.ReadBytes(winrt::array_view<uint8_t>(info.cover));
            }
        }

        return info;
    } catch (...) {
        return std::nullopt;
    }
}

void MediaTrackInfo::SetPosition(double seconds) {
    EnsureWinRtInitialized();

    try {
        auto manager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();

        if (auto session = manager.GetCurrentSession()) {
            int64_t ticks = static_cast<int64_t>(seconds * 10000000.0);
            session.TryChangePlaybackPositionAsync(ticks).get();
        }
    } catch (...) {}
}
