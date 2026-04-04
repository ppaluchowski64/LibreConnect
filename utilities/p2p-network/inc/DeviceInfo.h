#ifndef DEVICE_INFO_H
#define DEVICE_INFO_H

#include <vector>
#include <Packable.h>
#include <boost/uuid.hpp>
#include <DeviceTypeDetector.h>

class ConnectionManager;
class InitialConnection;
class DeviceData;

struct DeviceInfoLite {
    std::string deviceName;
    boost::uuids::uuid deviceID;
    DeviceType deviceType;
};

struct DeviceInfo {
    std::string deviceName;
    std::string deviceAddress;
    std::uint16_t deviceAddressPort;
    boost::uuids::uuid deviceID;
    DeviceType deviceType;
    std::string certificateFingerprint;

    static DeviceInfo GetThisDeviceInfo();

    void Serialize(std::vector<uint8_t>& buffer, size_t& offset) const {
        SerializeObject(deviceName, buffer, offset);
        SerializeObject(deviceAddressPort, buffer, offset);
        SerializeObject(deviceID, buffer, offset);
        SerializeObject(deviceType, buffer, offset);
        SerializeObject(certificateFingerprint, buffer, offset);
    }

    void Deserialize(const std::vector<uint8_t>& buffer, size_t& offset) {
        DeserializeObject(deviceName, buffer,offset);
        DeserializeObject(deviceAddressPort, buffer,offset);
        DeserializeObject(deviceID, buffer, offset);
        DeserializeObject(deviceType, buffer, offset);
        DeserializeObject(certificateFingerprint, buffer, offset);
    }

    inline size_t GetSerializedSize() const {
        return GetObjectSerializedSize(deviceName) +
            GetObjectSerializedSize(deviceAddressPort) +
            GetObjectSerializedSize(deviceID) +
            GetObjectSerializedSize(deviceType) +
            GetObjectSerializedSize(certificateFingerprint);
    }
};


#endif //DEVICE_INFO_H
