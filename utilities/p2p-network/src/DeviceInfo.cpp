#include <DeviceInfo.h>
#include <asio.hpp>
#include <ConnectionManager.h>
#include <DeviceData.h>

DeviceInfo DeviceInfo::GetThisDeviceInfo() {
    const TCPEndpoint endpoint = ConnectionManager::GetSeekEndpoint();
    DeviceInfo device{};

    device.deviceName = asio::ip::host_name();
    device.deviceID   = DeviceData::GetDeviceUUID();
    device.deviceAddressPort = endpoint.port();

    return device;
}
