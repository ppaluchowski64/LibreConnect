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
    struct Options {
        bool transmitProbes{true};
        bool emitEvents{true};
    };

    LanDeviceScanner();

    static void BeginScan(const Options& options = {true, true});
    static void EndScan();
    static void RestartScan();
    static std::vector<DeviceInfo> GetDiscoveredDevices();

private:
    asio::awaitable<void> Co_RestartScan() const;
    asio::awaitable<void> Co_JoinMulticastGroup();
    asio::awaitable<void> Co_LeaveMulticastGroup();
    asio::awaitable<void> Co_SendProbes() const;
    asio::awaitable<void> Co_ReceiveResponses();

    void ProcessError(const asio::system_error& error) const;

    static size_t GetTimeMS();
    static LanDeviceScanner* s_instance;

    IOContext& m_context;
    IOContextStrand m_strand;
    std::mutex m_mutex;

    std::unordered_map<boost::uuids::uuid, size_t> m_devicesLastProbe;

    std::unique_ptr<UDPSocket> m_outSocket;
    std::unique_ptr<UDPSocket> m_inSocket;

    std::vector<DeviceInfo> m_discoveredDevices;
    Options m_options{};
    std::atomic<bool> m_isScanning{false};
};

#endif //LAN_DEVICE_SCANNER_H
