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
        // RE: It was late and I probably got a bit too cautious ;)

        std::optional<std::string> name;
        std::optional<std::string> path;
        std::optional<uint64_t> size;
        std::optional<FileType> type;
        std::optional<int64_t> lastModTime;
        std::optional<int64_t> creationTime;

    public:
        FileEntry() = default;
        explicit FileEntry(const std::filesystem::path& filepath);

        [[nodiscard]] std::optional<std::string> GetName() const;
        [[nodiscard]] std::optional<std::string> GetPath() const;
        [[nodiscard]] std::optional<uint64_t> GetSize() const;
        [[nodiscard]] std::optional<FileType> GetType() const;
        [[nodiscard]] std::optional<int64_t> GetLastModTime() const;
        [[nodiscard]] std::optional<int64_t> GetCreationTime() const;

        void Serialize(std::vector<uint8_t>& buffer, size_t& offset) const;
        void Deserialize(const std::vector<uint8_t>& buffer, size_t& offset);
        [[nodiscard]] size_t GetSerializedSize() const;
};

#endif // FILE_ENTRY_H
