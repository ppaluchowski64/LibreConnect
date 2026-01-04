#ifndef FILE_TIME_UTILS_H
#define FILE_TIME_UTILS_H

#include <filesystem>
#include <ctime>

std::time_t GetLastModTime(const std::filesystem::path& path);
std::time_t GetCreationTime(const std::filesystem::path& path);

#endif // FILE_TIME_UTILS_H
