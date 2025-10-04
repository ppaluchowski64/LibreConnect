#ifndef LAN_DEVICE_SCANNER_H
#define LAN_DEVICE_SCANNER_H

#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <AsioCommon.h>
#include <AwaitableFlag.h>
#include <vector>
#include <Packable.h>

struct DeviceInfo {
    std::string deviceName;
    std::string macAddress;

    void Serialize(std::vector<uint8_t>& buffer, size_t& offset) const {
        SerializeObject(deviceName, buffer, offset);
        SerializeObject(macAddress, buffer, offset);
    }

    void Deserialize(const std::vector<uint8_t>& buffer, size_t& offset) {
        DeserializeObject(deviceName, buffer, offset);
        DeserializeObject(macAddress, buffer, offset);
    }

    constexpr size_t GetSerializedSize() const {
        return GetObjectSerializedSize(deviceName) + GetObjectSerializedSize(macAddress);
    }
};


class LanDeviceScanner {
public:
    LanDeviceScanner();

    static void BeginScan();
    static void EndScan();
    static std::vector<DeviceInfo> GetDiscoveredDevices();

private:
    asio::awaitable<void> Co_JoinMulticastGroup();
    asio::awaitable<void> Co_LeaveMulticastGroup();
    asio::awaitable<void> Co_SendProbes();
    asio::awaitable<void> Co_ReceiveResponses();

    static DeviceInfo GetDeviceInfo();
    static LanDeviceScanner* s_instance;

    IOContext m_context;

    AwaitableFlag m_awaitableFlag;
    IOWorkGuard m_workGuard;

    UDPSocket m_socket;

    std::thread m_contextThread;
    bool m_isScanning{false};

    std::vector<DeviceInfo> m_discoveredDevices;
};

#endif //LAN_DEVICE_SCANNER_H
