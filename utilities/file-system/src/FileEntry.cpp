#include "FileEntry.h"
#include "FileUtils.h"

#include <Packable.h>
#include <optional>
#include <string>
#include <filesystem>

FileEntry::FileEntry(const std::filesystem::path& filepath) {
    if (!std::filesystem::exists(filepath))
        return;

    name = filepath.filename().string();
    path = std::filesystem::absolute(filepath).parent_path().string();

    try {
        if (std::filesystem::is_regular_file(filepath))
            size = std::filesystem::file_size(filepath);
        else
            size = std::nullopt; // For now
    } catch (const std::filesystem::filesystem_error&) {
        size = std::nullopt;
    }

    type = DetectFileType(filepath);
    lastModTime = GetFileLastModTime(filepath);
    creationTime = GetFileCreationTime(filepath);
}

std::optional<std::string> FileEntry::GetName() const {
    return name;
}

std::optional<std::string> FileEntry::GetPath() const {
    return path;
}

std::optional<uint64_t> FileEntry::GetSize() const {
    return size;
}

std::optional<FileType> FileEntry::GetType() const {
    return type;
}

std::optional<int64_t> FileEntry::GetLastModTime() const {
    return lastModTime;
}

std::optional<int64_t> FileEntry::GetCreationTime() const {
    return creationTime;
}

void FileEntry::Serialize(std::vector<uint8_t>& buffer, size_t& offset) const {
    SerializeObject(name, buffer, offset);
    SerializeObject(path, buffer, offset);
    SerializeObject(size, buffer, offset);
    SerializeObject(type, buffer, offset);
    SerializeObject(lastModTime, buffer, offset);
    SerializeObject(creationTime, buffer, offset);
}

void FileEntry::Deserialize(const std::vector<uint8_t>& buffer, size_t& offset) {
    DeserializeObject(name, buffer, offset);
    DeserializeObject(path, buffer, offset);
    DeserializeObject(size, buffer, offset);
    DeserializeObject(type, buffer, offset);
    DeserializeObject(lastModTime, buffer, offset);
    DeserializeObject(creationTime, buffer, offset);
}

size_t FileEntry::GetSerializedSize() const {
    return GetObjectSerializedSize(name) +
           GetObjectSerializedSize(path) +
           GetObjectSerializedSize(size) +
           GetObjectSerializedSize(type) +
           GetObjectSerializedSize(lastModTime) +
           GetObjectSerializedSize(creationTime);
}
