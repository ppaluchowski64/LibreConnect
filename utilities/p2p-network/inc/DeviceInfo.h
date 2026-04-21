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
    std::string osName;
    std::string osVersion;
    std::string appVersion;
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
        SerializeObject(osName, buffer, offset);
        SerializeObject(osVersion, buffer, offset);
        SerializeObject(appVersion, buffer, offset);
    }

    void Deserialize(const std::vector<uint8_t>& buffer, size_t& offset) {
        DeserializeObject(deviceName, buffer,offset);
        DeserializeObject(deviceAddressPort, buffer,offset);
        DeserializeObject(deviceID, buffer, offset);
        DeserializeObject(deviceType, buffer, offset);
        DeserializeObject(certificateFingerprint, buffer, offset);
        if (offset < buffer.size()) {
            DeserializeObject(osName, buffer, offset);
        } else {
            osName.clear();
        }
        if (offset < buffer.size()) {
            DeserializeObject(osVersion, buffer, offset);
        } else {
            osVersion.clear();
        }
        if (offset < buffer.size()) {
            DeserializeObject(appVersion, buffer, offset);
        } else {
            appVersion.clear();
        }
    }

    inline size_t GetSerializedSize() const {
        return GetObjectSerializedSize(deviceName) +
            GetObjectSerializedSize(deviceAddressPort) +
            GetObjectSerializedSize(deviceID) +
            GetObjectSerializedSize(deviceType) +
            GetObjectSerializedSize(certificateFingerprint) +
            GetObjectSerializedSize(osName) +
            GetObjectSerializedSize(osVersion) +
            GetObjectSerializedSize(appVersion);
    }
};


#endif //DEVICE_INFO_H
