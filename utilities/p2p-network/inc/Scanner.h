#ifndef LAN_DEVICE_SCANNER_H
#define LAN_DEVICE_SCANNER_H

#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <AsioCommon.h>
#include <AwaitableFlag.h>
#include <vector>
#include <Packable.h>
#include <boost/uuid.hpp>
#include <DeviceInfo.h>

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

    std::mutex m_mutex;

    static size_t GetTimeMS();
    static LanDeviceScanner* s_instance;

    IOContext m_context;

    AwaitableFlag m_awaitableFlag;
    IOWorkGuard m_workGuard;

    UDPSocket m_socket;

    std::thread m_contextThread;
    bool m_isScanning{false};

    std::unordered_map<boost::uuids::uuid, size_t> m_devicesLastProbe;
    std::vector<DeviceInfo> m_discoveredDevices;
};

#endif //LAN_DEVICE_SCANNER_H
