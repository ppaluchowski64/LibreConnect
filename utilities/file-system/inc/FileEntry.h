#ifndef FILE_ENTRY_H
#define FILE_ENTRY_H

#include <optional>
#include <string>
#include <vector>
#include <filesystem>

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

class FileEntry {
    private:
        // Optional name and path ??? what ??

        std::optional<std::string> name;
        std::optional<std::string> path;
        std::optional<uint64_t> size;
        std::optional<FileType> type;
        std::optional<int64_t> lastModTime;
        std::optional<int64_t> creationTime;

    public:
        FileEntry() = default;
        explicit FileEntry(const std::filesystem::path& filepath);

        std::optional<std::string> GetName() const;
        std::optional<std::string> GetPath() const;
        std::optional<uint64_t> GetSize() const;
        std::optional<FileType> GetType() const;
        std::optional<int64_t> GetLastModTime() const;
        std::optional<int64_t> GetCreationTime() const;

        void Serialize(std::vector<uint8_t>& buffer, size_t& offset) const;
        void Deserialize(const std::vector<uint8_t>& buffer, size_t& offset);
        size_t GetSerializedSize() const;
};

#endif // FILE_ENTRY_H
