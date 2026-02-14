#include <TransferInfo.h>
#include <Packable.h>

void TransferInfo::Serialize(std::vector<uint8_t>& buffer, size_t& offset) const {
    SerializeObject(size, buffer, offset);
    SerializeObject(channel, buffer, offset);
}

void TransferInfo::Deserialize(const std::vector<uint8_t>& buffer, size_t& offset) {
    DeserializeObject(size, buffer, offset);
    DeserializeObject(channel, buffer, offset);
}

size_t TransferInfo::GetSerializedSize() const {
    return GetObjectSerializedSize(size) + GetObjectSerializedSize(channel);
}
