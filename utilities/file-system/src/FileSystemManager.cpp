#include "FileSystemManager.h"

#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QUrl>

#include <vector>
#include <cstdlib>
#include <filesystem>
#include <memory>

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

bool FileSystemManager::CopyToClipboard(const std::vector<std::filesystem::path>& paths) {
    if (paths.empty())
        return false;

    QList<QUrl> urlList;
    urlList.reserve(static_cast<int>(paths.size()));

    bool anyValid = false;

    for (const auto& p : paths) {
        if (!std::filesystem::exists(p))
            continue;

        urlList.append(QUrl::fromLocalFile(QString::fromStdString(p.string())));
        anyValid = true;
    }

    if (!anyValid)
        return false;

    auto mimeData = std::make_unique<QMimeData>();
    mimeData->setUrls(urlList);

    QClipboard* clipboard = QGuiApplication::clipboard();
    clipboard->setMimeData(mimeData.release());

    return true;
}
