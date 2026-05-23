#include "MediaRemote.h"
#include "NativeMediaMap.h"

#ifdef _WIN32
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <string>
#endif

#ifdef __APPLE__
    void EmitMacMediaSignal(int macCode);
#endif

#ifdef _WIN32
namespace {
    bool ControlPlaybackWinRT(MediaSignal signal) {
        try {
            thread_local bool winrtInitialized = false;
            if (!winrtInitialized) {
                try {
                    winrt::init_apartment();
                } catch (...) {}
                winrtInitialized = true;
            }

            auto manager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            auto session = manager.GetCurrentSession();

            if (session) {
                const std::string aumid = winrt::to_string(session.SourceAppUserModelId());
                if (aumid.find("LibreConnect") != std::string::npos) {
                    session = nullptr;
                }
            }

            if (!session) {
                auto sessions = manager.GetSessions();
                winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession fallbackSession{ nullptr };
                for (auto s : sessions) {
                    const std::string aumid = winrt::to_string(s.SourceAppUserModelId());
                    if (aumid.find("LibreConnect") != std::string::npos) {
                        continue;
                    }
                    if (!fallbackSession) {
                        fallbackSession = s;
                    }
                    auto playback = s.GetPlaybackInfo();
                    if (playback.PlaybackStatus() == winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
                        session = s;
                        break;
                    }
                }
                if (!session && fallbackSession) {
                    session = fallbackSession;
                }
            }

            if (!session)
                return false;

            switch (signal) {
                case MediaSignal::PlayPause:
                    session.TryTogglePlayPauseAsync().get();
                    return true;
                case MediaSignal::NextTrack:
                    session.TrySkipNextAsync().get();
                    return true;
                case MediaSignal::PreviousTrack:
                    session.TrySkipPreviousAsync().get();
                    return true;
                default:
                    break;
            }
        } catch (...) {}

        return false;
    }
}
#endif

MediaRemote::MediaRemote() : VirtualInputDevice("libreconnect-media-remote") {}
MediaRemote::~MediaRemote() = default;

void MediaRemote::PlayPause() {
    ExecuteSignal(MediaSignal::PlayPause);
}

void MediaRemote::NextTrack() {
    ExecuteSignal(MediaSignal::NextTrack);
}

void MediaRemote::PreviousTrack() {
    ExecuteSignal(MediaSignal::PreviousTrack);
}

void MediaRemote::VolumeUp() {
    ExecuteSignal(MediaSignal::VolumeUp);
}

void MediaRemote::VolumeDown() {
    ExecuteSignal(MediaSignal::VolumeDown);
}

void MediaRemote::VolumeMute() {
    ExecuteSignal(MediaSignal::VolumeMute);
}

void MediaRemote::ExecuteSignal(MediaSignal signal) {
    #ifdef _WIN32
        if (ControlPlaybackWinRT(signal)) {
            return;
        }
    #endif

    int nativeCode = GetNativeMediaCode(signal);
    if (nativeCode == -1) return;

    #ifdef __APPLE__
        EmitMacMediaSignal(nativeCode);
    #else
        EmitNativeKeyPress(nativeCode);
        EmitNativeKeyRelease(nativeCode);
    #endif
}
