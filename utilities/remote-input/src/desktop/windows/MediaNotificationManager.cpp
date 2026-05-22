#include "MediaNotificationManager.h"
#include "MediaTrackInfo.h"

#if !defined(NTDDI_VERSION) || NTDDI_VERSION < 0x0A000000
    #undef NTDDI_VERSION
    #define NTDDI_VERSION 0x0A000000
#endif

#include <Windows.h>
#include <SystemMediaTransportControlsInterop.h>
#include <propkey.h>
#include <propsys.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>

namespace {
    constexpr wchar_t kSmtcWindowClassName[] = L"LibreConnectMediaNotificationWindow";
    constexpr wchar_t kSmtcWindowTitle[] = L"LibreConnect Media";
    constexpr wchar_t kAppUserModelId[] = L"Default.LibreConnect.main.1.0";

    void EnsureWinRtInitialized() {
        thread_local bool initialized = false;

        if (!initialized) {
            try {
                winrt::init_apartment();
            } catch (...) {}

            initialized = true;
        }
    }

    void EnsureAppUserModelId() {
        using SetCurrentProcessExplicitAppUserModelIdProc = HRESULT(WINAPI*)(PCWSTR);

        static bool attempted = false;
        if (attempted)
            return;

        attempted = true;

        HMODULE shell32 = LoadLibraryW(L"shell32.dll");
        if (!shell32)
            return;

        auto setProcessAppId = reinterpret_cast<SetCurrentProcessExplicitAppUserModelIdProc>(
            GetProcAddress(shell32, "SetCurrentProcessExplicitAppUserModelID")
        );

        if (setProcessAppId)
            setProcessAppId(kAppUserModelId);
    }

    void SetWindowAppUserModelId(HWND hwnd) {
        using SHGetPropertyStoreForWindowProc = HRESULT(WINAPI*)(HWND, REFIID, void**);

        HMODULE shell32 = LoadLibraryW(L"shell32.dll");
        if (!shell32)
            return;

        auto getPropertyStoreForWindow = reinterpret_cast<SHGetPropertyStoreForWindowProc>(
            GetProcAddress(shell32, "SHGetPropertyStoreForWindow")
        );

        if (!getPropertyStoreForWindow)
            return;

        winrt::com_ptr<IPropertyStore> propertyStore;
        if (FAILED(getPropertyStoreForWindow(hwnd, IID_PPV_ARGS(propertyStore.put()))))
            return;

        PROPVARIANT appId{};
        appId.vt = VT_LPWSTR;
        appId.pwszVal = const_cast<PWSTR>(kAppUserModelId);

        if (SUCCEEDED(propertyStore->SetValue(PKEY_AppUserModel_ID, appId)))
            propertyStore->Commit();
    }

    LRESULT CALLBACK SmtcWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    HWND CreateSmtcWindow() {
        HINSTANCE instance = GetModuleHandleW(nullptr);

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = SmtcWindowProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = kSmtcWindowClassName;

        if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return nullptr;

        HWND hwnd = CreateWindowExW(
            0,
            kSmtcWindowClassName,
            kSmtcWindowTitle,
            WS_OVERLAPPED,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1,
            1,
            nullptr,
            nullptr,
            instance,
            nullptr
        );

        if (hwnd)
            SetWindowAppUserModelId(hwnd);

        return hwnd;
    }

    winrt::Windows::Media::SystemMediaTransportControls GetSmtcForWindow(HWND hwnd) {
        auto interop = winrt::get_activation_factory<
            winrt::Windows::Media::SystemMediaTransportControls,
            ISystemMediaTransportControlsInterop>();

        winrt::Windows::Media::SystemMediaTransportControls controls{ nullptr };
        winrt::check_hresult(interop->GetForWindow(
            hwnd,
            winrt::guid_of<winrt::Windows::Media::ISystemMediaTransportControls>(),
            winrt::put_abi(controls)
        ));

        return controls;
    }

    std::mutex g_mutex;
    std::function<void(MediaSignal)> g_actionCallback;
    std::function<void(double)> g_seekCallback;

    HWND g_window = nullptr;
    winrt::Windows::Media::SystemMediaTransportControls g_smtc{ nullptr };
    winrt::Windows::Storage::Streams::RandomAccessStreamReference g_thumbnail{ nullptr };

    winrt::event_token g_buttonToken;
    winrt::event_token g_seekToken;

    TrackMetadata g_metadata;
    std::filesystem::path g_coverPath;
    uint64_t g_coverVersion = 0;

    std::wstring CoverFileExtension(const std::vector<uint8_t>& cover) {
        if (cover.size() >= 8 &&
            cover[0] == 0x89 && cover[1] == 0x50 && cover[2] == 0x4E && cover[3] == 0x47 &&
            cover[4] == 0x0D && cover[5] == 0x0A && cover[6] == 0x1A && cover[7] == 0x0A) {
            return L".png";
        }

        if (cover.size() >= 3 && cover[0] == 0xFF && cover[1] == 0xD8 && cover[2] == 0xFF)
            return L".jpg";

        if (cover.size() >= 6 &&
            cover[0] == 0x47 && cover[1] == 0x49 && cover[2] == 0x46 && cover[3] == 0x38) {
            return L".gif";
        }

        if (cover.size() >= 2 && cover[0] == 0x42 && cover[1] == 0x4D)
            return L".bmp";

        if (cover.size() >= 12 &&
            cover[0] == 0x52 && cover[1] == 0x49 && cover[2] == 0x46 && cover[3] == 0x46 &&
            cover[8] == 0x57 && cover[9] == 0x45 && cover[10] == 0x42 && cover[11] == 0x50) {
            return L".webp";
        }

        return L".jpg";
    }

    std::filesystem::path CoverCacheDirectory() {
        std::wstring tempPath(MAX_PATH, L'\0');
        DWORD length = GetTempPathW(static_cast<DWORD>(tempPath.size()), tempPath.data());

        if (length == 0 || length > tempPath.size()) {
            tempPath = L".";
        } else {
            tempPath.resize(length);
        }

        std::filesystem::path directory = std::filesystem::path(tempPath) / L"LibreConnect";
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        return directory;
    }

    void RemoveCachedCover(const std::filesystem::path& path) {
        if (path.empty())
            return;

        std::error_code error;
        std::filesystem::remove(path, error);
    }

    std::optional<std::filesystem::path> WriteCoverToCache(const std::vector<uint8_t>& cover) {
        if (cover.empty())
            return std::nullopt;

        const std::filesystem::path path =
            CoverCacheDirectory() /
            (L"media_cover_" + std::to_wstring(++g_coverVersion) + CoverFileExtension(cover));

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
            return std::nullopt;

        file.write(reinterpret_cast<const char*>(cover.data()), static_cast<std::streamsize>(cover.size()));
        if (!file)
            return std::nullopt;

        return path;
    }
}

void MediaNotificationManager::Show() {
    EnsureWinRtInitialized();
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_smtc)
        return;

    EnsureAppUserModelId();

    if (!g_window)
        g_window = CreateSmtcWindow();

    if (!g_window)
        return;

    g_smtc = GetSmtcForWindow(g_window);
    g_smtc.IsEnabled(true);
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

    if (!g_smtc)
        return;

    g_smtc.ButtonPressed(g_buttonToken);
    g_smtc.PlaybackPositionChangeRequested(g_seekToken);

    g_thumbnail = nullptr;
    RemoveCachedCover(g_coverPath);
    g_coverPath.clear();

    g_smtc.IsEnabled(false);
    g_smtc = nullptr;

    if (g_window) {
        DestroyWindow(g_window);
        g_window = nullptr;
    }
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
        const auto oldCoverPath = g_coverPath;
        const auto newCoverPath = WriteCoverToCache(metadata.cover);

        if (newCoverPath) {
            auto file = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(newCoverPath->wstring()).get();
            g_thumbnail = winrt::Windows::Storage::Streams::RandomAccessStreamReference::CreateFromFile(file);
            g_coverPath = *newCoverPath;
            updater.Thumbnail(g_thumbnail);
            RemoveCachedCover(oldCoverPath);
        } else {
            g_thumbnail = nullptr;
            g_coverPath.clear();
            updater.Thumbnail(nullptr);
            RemoveCachedCover(oldCoverPath);
        }
    } else {
        g_thumbnail = nullptr;
        RemoveCachedCover(g_coverPath);
        g_coverPath.clear();
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
