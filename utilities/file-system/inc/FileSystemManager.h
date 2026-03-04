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
        static DirectoryResult GetEntries(const std::filesystem::path& dirPath);

        static bool CopyToClipboard(const std::vector<std::filesystem::path>& paths);
        static bool CopyToClipboard(const std::filesystem::path& path);
        static bool PasteFromClipboard(const std::filesystem::path& targetDir);
        static bool FilesInClipboard();
};

class TextClipboard {
    public:
        static bool Set(const std::string& text);
        static std::string Get();
        static bool Has();

    private:
    #ifdef __linux__
        static bool IsWayland();
        static bool HasWlClipboard();
    #endif
};

#endif // FILE_SYSTEM_MANAGER_H
