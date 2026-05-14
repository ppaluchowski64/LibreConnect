#include "FileSystemManager.h"

#include <vector>
#include <cstdlib>
#include <chrono>
#ifdef _WIN32
#include <cstdint>
#include <cstring>
#endif
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <QGuiApplication>
#include <QClipboard>
#include <QByteArray>
#include <QMimeData>
#include <QUrl>
#include <QMetaObject>
#include <QThread>

#if defined(__linux__) || defined(__APPLE__) || defined(__ANDROID__)
#include <dirent.h>
#endif

namespace
{
constexpr const char* TEMP_STORAGE_ROOT_FOLDER = "LibreConnect";
constexpr const char* TEMP_STORAGE_DEDICATED_FOLDER = "temporary-storage";

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
    return {value.toStdWString()};
#else
    return {value.toStdString()};
#endif
}

template <typename Fn>
auto InvokeOnGuiThreadSync(Fn&& fn) -> decltype(fn())
{
    using ReturnType = decltype(fn());

    QCoreApplication* app = QGuiApplication::instance();
    if (!app) {
        return {};
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

std::filesystem::path NormalizePath(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(path, ec);
    if (!ec) {
        return canonicalPath;
    }

    return path.lexically_normal();
}

bool IsWithinRoot(const std::filesystem::path& path, const std::filesystem::path& root)
{
    const std::filesystem::path normalizedPath = NormalizePath(path);
    const std::filesystem::path normalizedRoot = NormalizePath(root);
    const std::filesystem::path relativePath = normalizedPath.lexically_relative(normalizedRoot);
    if (relativePath.empty()) {
        return false;
    }

    const std::string relativeGeneric = relativePath.generic_string();
    return relativeGeneric != ".." && !relativeGeneric.starts_with("../");
}

bool IsValidTemporaryStorageRoot(const std::filesystem::path& rootPath)
{
    if (rootPath.empty()) {
        return false;
    }

    std::filesystem::path tempRoot;
    try {
        tempRoot = std::filesystem::temp_directory_path();
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }

    if (tempRoot.empty()) {
        return false;
    }

    const std::filesystem::path normalizedRootPath = NormalizePath(rootPath);
    if (normalizedRootPath.filename() != TEMP_STORAGE_DEDICATED_FOLDER) {
        return false;
    }

    const std::filesystem::path parentPath = normalizedRootPath.parent_path();
    if (parentPath.filename() != TEMP_STORAGE_ROOT_FOLDER) {
        return false;
    }

    return IsWithinRoot(normalizedRootPath, NormalizePath(tempRoot));
}

void RemoveEmptyParents(const std::filesystem::path& startPath, const std::filesystem::path& stopRoot)
{
    const std::filesystem::path normalizedStopRoot = NormalizePath(stopRoot);
    std::filesystem::path current = NormalizePath(startPath);

    while (!current.empty() && current != normalizedStopRoot) {
        if (!IsWithinRoot(current, normalizedStopRoot)) {
            break;
        }

        std::error_code ec;
        if (!std::filesystem::is_directory(current, ec) || ec) {
            break;
        }

        if (!std::filesystem::is_empty(current, ec) || ec) {
            break;
        }

        std::filesystem::remove(current, ec);
        if (ec) {
            break;
        }

        current = current.parent_path();
    }
}

void CleanupRootsAsync(std::vector<std::filesystem::path> cleanupRoots)
{
    if (cleanupRoots.empty()) {
        return;
    }

    const std::filesystem::path dedicatedRoot = FileSystemManager::GetTemporaryStoragePath();
    if (dedicatedRoot.empty()) {
        return;
    }

    std::thread([cleanupRoots = std::move(cleanupRoots), dedicatedRoot]() mutable {
        constexpr int MAX_ATTEMPTS = 120;
        constexpr auto RETRY_DELAY = std::chrono::seconds(1);

        for (int attempt = 0; attempt < MAX_ATTEMPTS && !cleanupRoots.empty(); ++attempt) {
            std::vector<std::filesystem::path> pendingRoots;
            pendingRoots.reserve(cleanupRoots.size());

            for (const std::filesystem::path& root : cleanupRoots) {
                if (!IsWithinRoot(root, dedicatedRoot)) {
                    continue;
                }

                std::error_code existsError;
                if (!std::filesystem::exists(root, existsError) || existsError) {
                    RemoveEmptyParents(root.parent_path(), dedicatedRoot);
                    continue;
                }

                std::error_code removeError;
                std::filesystem::remove_all(root, removeError);
                if (removeError) {
                    pendingRoots.push_back(root);
                    continue;
                }

                RemoveEmptyParents(root.parent_path(), dedicatedRoot);
            }

            cleanupRoots.swap(pendingRoots);
            if (!cleanupRoots.empty()) {
                std::this_thread::sleep_for(RETRY_DELAY);
            }
        }
    }).detach();
}

class TemporaryClipboardMimeData final : public QMimeData
{
public:
    explicit TemporaryClipboardMimeData(std::vector<std::filesystem::path> cleanupRoots)
        : m_cleanupRoots(std::move(cleanupRoots))
    {
    }

    ~TemporaryClipboardMimeData() override
    {
        CleanupRootsAsync(std::move(m_cleanupRoots));
    }

private:
    std::vector<std::filesystem::path> m_cleanupRoots;
};
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

std::filesystem::path FileSystemManager::GetTemporaryStoragePath()
{
    std::filesystem::path rootPath;

    try {
        rootPath = std::filesystem::temp_directory_path() / TEMP_STORAGE_ROOT_FOLDER / TEMP_STORAGE_DEDICATED_FOLDER;
    } catch (const std::filesystem::filesystem_error&) {
        return {};
    }

    std::error_code ec;
    std::filesystem::create_directories(rootPath, ec);
    if (ec) {
        return {};
    }

    if (!IsValidTemporaryStorageRoot(rootPath)) {
        return {};
    }

    return rootPath;
}

std::filesystem::path FileSystemManager::GetTemporaryStoragePath(const std::string& category)
{
    std::filesystem::path basePath = GetTemporaryStoragePath();
    if (basePath.empty() || category.empty()) {
        return basePath;
    }

#if defined(__cpp_lib_char8_t)
    std::filesystem::path categoryPath = basePath / std::filesystem::path(reinterpret_cast<const char8_t*>(category.c_str()));
#else
    std::filesystem::path categoryPath = basePath / std::filesystem::u8path(category);
#endif

    std::error_code ec;
    std::filesystem::create_directories(categoryPath, ec);
    if (ec) {
        return {};
    }

    return categoryPath;
}

bool FileSystemManager::ClearTemporaryStorage()
{
    const std::filesystem::path rootPath = GetTemporaryStoragePath();
    if (rootPath.empty()) {
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::exists(rootPath, ec) || ec) {
        return !ec;
    }

    for (const auto& entry : std::filesystem::directory_iterator(rootPath, ec)) {
        if (ec) {
            return false;
        }

        if (!IsWithinRoot(entry.path(), rootPath)) {
            return false;
        }

        std::filesystem::remove_all(entry.path(), ec);
        if (ec) {
            return false;
        }
    }

    return true;
}


DirectoryResult FileSystemManager::GetEntries(const std::filesystem::path& dirPath) {
    DirectoryResult result;

#if defined(__linux__) || defined(__APPLE__) || defined(__ANDROID__)
    DIR* dir = opendir(dirPath.c_str());
    if (!dir) {
        return result;
    }

    result.entries.reserve(2048);

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.' &&
           (entry->d_name[1] == '\0' || (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
            continue;
           }

        result.entries.emplace_back(dirPath / entry->d_name);
    }

    closedir(dir);
    result.success = true;

#else
    std::error_code ec;
    if (!std::filesystem::is_directory(dirPath, ec) || ec) {
        return result;
    }

    constexpr std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::directory_iterator it(dirPath, options, ec);

    if (ec) {
        return result;
    }

    result.entries.reserve(2048);
    for (const auto& entry : it) {
        result.entries.emplace_back(entry.path());
    }

    result.success = true;
#endif

    return result;
}

bool FileSystemManager::CopyToClipboard(const std::vector<std::filesystem::path>& paths) {
    return CopyToClipboard(paths, {});
}

bool FileSystemManager::CopyToClipboard(const std::vector<std::filesystem::path>& paths, std::vector<std::filesystem::path> cleanupRoots) {
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

    return InvokeOnGuiThreadSync([urlList, hasCleanupRoots = !cleanupRoots.empty(), cleanupRoots = std::move(cleanupRoots)]() mutable {
        auto mimeData = std::make_unique<TemporaryClipboardMimeData>(std::move(cleanupRoots));
        mimeData->setUrls(urlList);
#ifdef _WIN32
        if (hasCleanupRoots && !mimeData->urls().empty()) {
            const uint32_t moveDropEffect = 2U; // DROPEFFECT_MOVE
            QByteArray preferredDropEffectData;
            preferredDropEffectData.resize(static_cast<int>(sizeof(moveDropEffect)));
            std::memcpy(preferredDropEffectData.data(), &moveDropEffect, sizeof(moveDropEffect));
            mimeData->setData(
                QStringLiteral("application/x-qt-windows-mime;value=\"Preferred DropEffect\""),
                preferredDropEffectData
            );
        }
#endif

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
