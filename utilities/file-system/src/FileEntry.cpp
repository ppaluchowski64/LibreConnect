#include "FileEntry.h"
#include <Packable.h>

void FileEntry::Serialize(std::vector<uint8_t>& buffer, size_t& offset) const {
    SerializeObject(name, buffer, offset);
    SerializeObject(path, buffer, offset);
    SerializeObject(size, buffer, offset);
    SerializeObject(type, buffer, offset);
    SerializeObject(lastModTime, buffer, offset);
}

void FileEntry::Deserialize(const std::vector<uint8_t>& buffer, size_t& offset) {
    DeserializeObject(name, buffer, offset);
    DeserializeObject(path, buffer, offset);
    DeserializeObject(size, buffer, offset);
    DeserializeObject(type, buffer, offset);
    DeserializeObject(lastModTime, buffer, offset);
}

size_t FileEntry::GetSerializedSize() const {
    return GetObjectSerializedSize(name) +
           GetObjectSerializedSize(path) +
           GetObjectSerializedSize(size) +
           GetObjectSerializedSize(type) +
           GetObjectSerializedSize(lastModTime);
}
