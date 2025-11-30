#ifndef FILE_ENTRY_H
#define FILE_ENTRY_H

#include <string>
#include <vector>
#include <cstdint>

enum class FileType : uint8_t {
    Directory,
    Text,
    Image,
    Video,
    Audio,
    Document,
    Archive,
    Executable,
    Unknown
};

FileType DetectFileType(const std::string& filepath);

struct FileEntry {
    std::string name;
    std::string path;
    uint64_t size;
    FileType type;
    uint64_t lastModTime;

    void Serialize(std::vector<uint8_t>& buffer, size_t& offset) const;
    void Deserialize(const std::vector<uint8_t>& buffer, size_t& offset);
    [[nodiscard]] size_t GetSerializedSize() const;
};

#endif // FILE_ENTRY_H
