#ifndef FILE_ENTRY_H
#define FILE_ENTRY_H

#include <string>
#include <vector>
#include <cstdint>

enum class FileType {
    Text,
    Image,
    Video,
    Audio,
    Binary,
    Archive,
    Document,
    Directory,
    Unknown
};

struct FileEntry {
    std::string name;
    std::string path;
    uint64_t size;
    FileType type;
    uint64_t lastModTime;

    void Serialize(std::vector<uint8_t>& buffer, size_t& offset) const;
    void Deserialize(const std::vector<uint8_t>& buffer, size_t& offset);
    size_t GetSerializedSize() const;
};

#endif // FILE_ENTRY_H
