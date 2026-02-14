#ifndef TRANSFER_INFO_H
#define TRANSFER_INFO_H

#include <vector>

struct TransferInfo {
    uint64_t size;
    uint8_t channel;

    void Serialize(std::vector<uint8_t>& buffer, size_t& offset) const;
    void Deserialize(const std::vector<uint8_t>& buffer, size_t& offset);
    size_t GetSerializedSize() const;
};

#endif // TRANSFER_INFO_H