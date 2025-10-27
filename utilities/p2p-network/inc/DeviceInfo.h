#ifndef DEVICE_INFO_H
#define DEVICE_INFO_H

#include <vector>
#include <../../system/inc/Packable.h>
#include <boost/uuid.hpp>
#include <asio.hpp>
#include <../../p2p-network/inc/DeviceData.h>
#include <ConnectionManager.h>

class DeviceData;

struct DeviceInfo {
    std::string deviceName;
    std::string deviceAddress;
    std::uint16_t deviceAddressPort;
    boost::uuids::uuid deviceID;

    static DeviceInfo GetThisDeviceInfo() {
        const TCPEndpoint endpoint = ConnectionManager::GetSeekEndpoint();
        DeviceInfo device{};

        device.deviceName = asio::ip::host_name();
        device.deviceID   = DeviceData::GetDeviceUUID();
        device.deviceAddressPort = endpoint.port();

        return device;
    }

    void Serialize(std::vector<uint8_t>& buffer, size_t& offset) const {
        SerializeObject(deviceName, buffer, offset);
        SerializeObject(deviceAddressPort, buffer, offset);
        SerializeObject(deviceID, buffer, offset);
    }

    void Deserialize(const std::vector<uint8_t>& buffer, size_t& offset) {
        DeserializeObject(deviceName, buffer,offset);
        DeserializeObject(deviceAddressPort, buffer,offset);
        DeserializeObject(deviceID, buffer, offset);
    }

    inline size_t GetSerializedSize() const {
        return GetObjectSerializedSize(deviceName) + GetObjectSerializedSize(deviceAddressPort) + GetObjectSerializedSize(deviceID);
    }
};


#endif //DEVICE_INFO_H