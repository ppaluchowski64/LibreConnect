#include "FileSystemManager.h"

#include <vector>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <utility>

#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QUrl>
#include <QMetaObject>
#include <QThread>

namespace
{
QString PathToQString(const std::filesystem::path& path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

std::filesystem::path QStringToPath(const QString& value)
{
#ifdef _WIN32
    return std::filesystem::path(value.toStdWString());
#else
    return std::filesystem::path(value.toStdString());
#endif
}

template <typename Fn>
auto InvokeOnGuiThreadSync(Fn&& fn) -> decltype(fn())
{
    using ReturnType = decltype(fn());

    QCoreApplication* app = QGuiApplication::instance();
    if (!app) {
        return ReturnType{};
    }

    if (QThread::currentThread() == app->thread()) {
        return fn();
    }

    ReturnType result{};
    QMetaObject::invokeMethod(
        app,
        [&result, callable = std::forward<Fn>(fn)]() mutable {
            result = callable();
        },
        Qt::BlockingQueuedConnection
    );

    return result;
}
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

        urlList.append(QUrl::fromLocalFile(PathToQString(path)));
        anyCopied = true;
    }

    if (!anyCopied)
        return false;

    return InvokeOnGuiThreadSync([urlList]() mutable {
        auto mimeData = std::make_unique<QMimeData>();
        mimeData->setUrls(urlList);

        QClipboard* const clipboard = QGuiApplication::clipboard();
        if (!clipboard) {
            return false;
        }

        clipboard->setMimeData(mimeData.release());
        return true;
    });
}

bool FileSystemManager::CopyToClipboard(const std::filesystem::path& path) {
    return CopyToClipboard(std::vector{ path });
}

bool FileSystemManager::PasteFromClipboard(const std::filesystem::path& targetDir) {
    if (!QGuiApplication::instance())
        return false;

    return InvokeOnGuiThreadSync([targetDir]() -> bool {
        const QClipboard* const clipboard = QGuiApplication::clipboard();
        if (!clipboard) {
            return false;
        }

        const QMimeData* const mime = clipboard->mimeData();
        if (!mime || !mime->hasUrls())
            return false;

        if (!std::filesystem::is_directory(targetDir))
            return false;

        bool anyPasted = false;

        for (const auto& url : mime->urls()) {
            if (!url.isLocalFile())
                continue;

            std::filesystem::path path = QStringToPath(url.toLocalFile());
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
    });
}

bool FileSystemManager::FilesInClipboard() {
    if (!QGuiApplication::instance())
        return false;

    return InvokeOnGuiThreadSync([]() -> bool {
        const QClipboard* const clipboard = QGuiApplication::clipboard();
        if (!clipboard) {
            return false;
        }

        const QMimeData* const mime = clipboard->mimeData();
        if (!mime || !mime->hasUrls())
            return false;

        bool anyFound = false;

        for (const auto& url : mime->urls()) {
            if (!url.isLocalFile())
                continue;

            std::filesystem::path path = QStringToPath(url.toLocalFile());
            if (!std::filesystem::exists(path))
                continue;

            anyFound = true;
            break;
        }

        return anyFound;
    });
}
