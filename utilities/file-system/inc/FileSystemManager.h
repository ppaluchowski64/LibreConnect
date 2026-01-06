#ifndef FILE_SYSTEM_MANAGER_H
#define FILE_SYSTEM_MANAGER_H

#include "FileEntry.h"

#include <vector>
#include <filesystem>

struct DirectoryResult {
    std::vector<FileEntry> entries;
    bool success = false;
};

class FileSystemManager {
    public:
        static DirectoryResult GetEntries(const std::filesystem::path& dirPath);
};

#endif // FILE_SYSTEM_MANAGER_H
