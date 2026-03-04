#include "FileSystemManager.h"

#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QUrl>

#include <vector>
#include <cstdlib>
#include <filesystem>
#include <memory>

#ifdef __linux__
    bool TextClipboard::IsWayland() {
        const char* session = std::getenv("XDG_SESSION_TYPE");
        if (session && std::strcmp(session, "wayland") == 0)
            return true;

        return std::getenv("WAYLAND_DISPLAY") != nullptr;
    }

    bool TextClipboard::HasWlClipboard() {
            return std::system("/bin/sh -c 'command -v wl-copy >/dev/null 2>&1'") == 0 &&
                   std::system("/bin/sh -c 'command -v wl-paste >/dev/null 2>&1'") == 0;
        }
#endif

std::filesystem::path FileSystemManager::GetAppDataPath(const std::string& appName) {
    std::filesystem::path basePath;

    #ifdef _WIN32
        if (const char* appData = std::getenv("APPDATA"))
            basePath = appData;

    #elif defined(__APPLE__)
        if (const char* home = std::getenv("HOME"))
            basePath = std::filesystem::path(home) / "Library" / "Application Support";

    #elif defined(__linux__)
        if (const char* xdg = std::getenv("XDG_DATA_HOME"))
            basePath = xdg;
        else if (const char* home = std::getenv("HOME"))
            basePath = std::filesystem::path(home) / ".local" / "share";

    #else
        // Only Windows, macOS and Linux are supported
        return {};
    #endif

    if (basePath.empty())
        return {};

    std::filesystem::path appDataPath = basePath / appName;

    try {
        std::filesystem::create_directories(appDataPath);
    } catch (const std::filesystem::filesystem_error&) {
        return {};
    }

    return appDataPath;
}

DirectoryResult FileSystemManager::GetEntries(const std::filesystem::path& dirPath) {
    DirectoryResult result;

    if (!std::filesystem::exists(dirPath) || !std::filesystem::is_directory(dirPath))
        return result;

    try {
        for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
            result.entries.emplace_back(entry.path());
        }
        result.success = true;
    } catch (const std::filesystem::filesystem_error&) {}

    return result;
}

bool FileSystemManager::CopyToClipboard(const std::vector<std::filesystem::path>& paths) {
    if (!QGuiApplication::instance() || paths.empty())
        return false;

    QList<QUrl> urlList;
    urlList.reserve(static_cast<int>(paths.size()));

    bool anyCopied = false;

    for (const auto& path : paths) {
        if (!std::filesystem::exists(path))
            continue;

        urlList.append(QUrl::fromLocalFile(QString::fromStdString(path.string())));
        anyCopied = true;
    }

    if (!anyCopied)
        return false;

    auto mimeData = std::make_unique<QMimeData>();
    mimeData->setUrls(urlList);

    QClipboard* const clipboard = QGuiApplication::clipboard();
    clipboard->setMimeData(mimeData.release());

    return true;
}

bool FileSystemManager::CopyToClipboard(const std::filesystem::path& path) {
    return CopyToClipboard(std::vector{ path });
}

bool FileSystemManager::PasteFromClipboard(const std::filesystem::path& targetDir) {
    if (!QGuiApplication::instance())
        return false;

    const QClipboard* const clipboard = QGuiApplication::clipboard();
    const QMimeData* const mime = clipboard->mimeData();

    if (!mime->hasUrls())
        return false;

    if (!std::filesystem::is_directory(targetDir))
        return false;

    bool anyPasted = false;

    for (const auto& url : mime->urls()) {
        if (!url.isLocalFile())
            continue;

        std::filesystem::path path = url.toLocalFile().toStdString();
        if (!std::filesystem::exists(path))
            continue;

        try {
            std::filesystem::copy(
                path,
                targetDir / path.filename(),
                std::filesystem::copy_options::recursive |
                std::filesystem::copy_options::overwrite_existing
            );
            anyPasted = true;
        } catch (const std::filesystem::filesystem_error&) {}
    }

    return anyPasted;
}

bool FileSystemManager::FilesInClipboard() {
    if (!QGuiApplication::instance())
        return false;

    const QClipboard* const clipboard = QGuiApplication::clipboard();
    const QMimeData* const mime = clipboard->mimeData();

    if (!mime->hasUrls())
        return false;

    bool anyFound = false;

    for (const auto& url : mime->urls()) {
        if (!url.isLocalFile())
            continue;

        std::filesystem::path path = url.toLocalFile().toStdString();
        if (!std::filesystem::exists(path))
            continue;

        anyFound = true;
        break;
    }

    return anyFound;
}

bool TextClipboard::Set(const std::string& text) {
    if (text.empty())
        return false;

    #ifdef __linux__
        if (IsWayland() && HasWlClipboard()) {
            std::string cmd = "wl-copy";
            FILE* pipe = popen(cmd.c_str(), "w");
            if (!pipe)
                return false;

            fwrite(text.c_str(), 1, text.size(), pipe);
            pclose(pipe);
            return true;
        }
    #endif

    if (!QGuiApplication::instance() || text.empty())
        return false;

    QClipboard* const clipboard = QGuiApplication::clipboard();
    clipboard->setText(QString::fromUtf8(text.c_str()));

    return true;
}

std::string TextClipboard::Get() {
    #ifdef __linux__
        if (IsWayland() && HasWlClipboard()) {
            std::string result;
            char buffer[256];

            FILE* pipe = popen("wl-paste -n", "r");
            if (!pipe)
                return {};

            while (fgets(buffer, sizeof(buffer), pipe))
                result += buffer;

            pclose(pipe);
            return result;
        }
    #endif

    if (!QGuiApplication::instance())
        return {};

    const QClipboard* const clipboard = QGuiApplication::clipboard();
    const QString text = clipboard->text();

    return text.toStdString();
}

bool TextClipboard::Has() {
    #ifdef __linux__
        if (IsWayland() && HasWlClipboard()) {
            return std::system(
                "/bin/sh -c 'wl-paste -n >/dev/null 2>&1'"
            ) == 0;
        }
    #endif

    if (!QGuiApplication::instance())
        return false;

    const QClipboard* const clipboard = QGuiApplication::clipboard();
    const QMimeData* const mime = clipboard->mimeData();

    return mime->hasText();
}
