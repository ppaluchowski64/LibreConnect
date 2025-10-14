#ifndef DEVICE_INFO_H
#define DEVICE_INFO_H

#include <vector>
#include <Packable.h>
#include <boost/uuid.hpp>

struct DeviceInfo {
    std::string deviceName;
    boost::uuids::uuid deviceID;

    void Serialize(std::vector<uint8_t>& buffer, size_t& offset) const {
        SerializeObject(deviceName, buffer, offset);
        SerializeObject(deviceID, buffer, offset);
    }

    void Deserialize(const std::vector<uint8_t>& buffer, size_t& offset) {
        DeserializeObject(deviceName, buffer,offset);
        DeserializeObject(deviceID, buffer, offset);
    }

    constexpr size_t GetSerializedSize() const {
        return GetObjectSerializedSize(deviceName) + GetObjectSerializedSize(deviceID);
    }
};


#endif //DEVICE_INFO_H