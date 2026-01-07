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
        static DirectoryResult GetEntries(const std::filesystem::path& dirPath);
        static std::filesystem::path GetAppDataPath(const std::string& appName);

        static bool CopyToClipboard(const std::vector<std::filesystem::path>& paths);
};

#endif // FILE_SYSTEM_MANAGER_H
