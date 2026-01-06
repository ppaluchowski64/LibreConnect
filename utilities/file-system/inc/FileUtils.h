#ifndef FILE_UTILS_H
#define FILE_UTILS_H

#include "FileEntry.h"

#include <optional>
#include <filesystem>

std::optional<FileType> DetectFileType(const std::filesystem::path& path);
// Placeholder for function header to calculate directory size
std::optional<std::time_t> GetFileLastModTime(const std::filesystem::path& path);
std::optional<std::time_t> GetFileCreationTime(const std::filesystem::path& path);

#endif // FILE_UTILS_H
