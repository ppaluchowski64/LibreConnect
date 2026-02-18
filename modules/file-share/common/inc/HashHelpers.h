#ifndef HASH_HELPERS_H
#define HASH_HELPERS_H

#include <filesystem>

size_t HashFile(const std::filesystem::path& path);
size_t HashString(const std::string& str);

#endif // HASH_HELPERS_H