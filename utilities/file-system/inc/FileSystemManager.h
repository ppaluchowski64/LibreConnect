#ifndef FILE_SYSTEM_MANAGER_H
#define FILE_SYSTEM_MANAGER_H

#include "FileEntry.h"

#include <string>
#include <vector>
#include <filesystem>

struct DirectoryResult {
    std::vector<FileEntry> entries;
    bool success = false;
};

class FileSystemManager {
    public:
        static std::filesystem::path GetAppDataPath(const std::string& appName);
        static std::filesystem::path GetTemporaryStoragePath();
        static std::filesystem::path GetTemporaryStoragePath(const std::string& category);
        static bool ClearTemporaryStorage();
        static DirectoryResult GetEntries(const std::filesystem::path& dirPath);

        static bool CopyToClipboard(const std::vector<std::filesystem::path>& paths);
        static bool CopyToClipboard(const std::vector<std::filesystem::path>& paths, std::vector<std::filesystem::path> cleanupRoots);
        static bool CopyToClipboard(const std::filesystem::path& path);
        static bool PasteFromClipboard(const std::filesystem::path& targetDir);
        static bool FilesInClipboard();
};

#endif // FILE_SYSTEM_MANAGER_H
