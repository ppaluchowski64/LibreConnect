#include "FileEntry.h"
#include <Packable.h>

void FileEntry::Serialize(std::vector<uint8_t>& buffer, size_t& offset) const {
    SerializeObject(name, buffer, offset);
    SerializeObject(path, buffer, offset);
    SerializeObject(size, buffer, offset);
    SerializeObject(static_cast<uint8_t>(type), buffer, offset);
    SerializeObject(lastModTime, buffer, offset);
}

void FileEntry::Deserialize(const std::vector<uint8_t>& buffer, size_t& offset) {
    DeserializeObject(name, buffer, offset);
    DeserializeObject(path, buffer, offset);
    DeserializeObject(size, buffer, offset);
    uint8_t rawType;
    DeserializeObject(rawType, buffer, offset);
    type = static_cast<FileType>(rawType);
    DeserializeObject(lastModTime, buffer, offset);
}

size_t FileEntry::GetSerializedSize() const {
    auto rawType = static_cast<uint8_t>(type);
    return GetObjectSerializedSize(name) +
           GetObjectSerializedSize(path) +
           GetObjectSerializedSize(size) +
           GetObjectSerializedSize(rawType) +
           GetObjectSerializedSize(lastModTime);
}
