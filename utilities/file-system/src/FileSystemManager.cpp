#include "FileSystemManager.h"

#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QUrl>

#include <vector>
#include <cstdlib>
#include <filesystem>
#include <memory>

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

    QClipboard* clipboard = QGuiApplication::clipboard();
    clipboard->setMimeData(mimeData.release());

    return true;
}

bool FileSystemManager::CopyToClipboard(const std::filesystem::path& path) {
    return CopyToClipboard(std::vector{ path });
}

bool FileSystemManager::PasteFromClipboard(const std::filesystem::path& targetDir) {
    if (!QGuiApplication::instance())
        return false;

    QClipboard* clipboard = QGuiApplication::clipboard();
    const QMimeData* mime = clipboard->mimeData();

    if (!mime || !mime->hasUrls())
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

    const QClipboard* clipboard = QGuiApplication::clipboard();
    const QMimeData* mime = clipboard->mimeData();

    if (!mime || !mime->hasUrls())
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
