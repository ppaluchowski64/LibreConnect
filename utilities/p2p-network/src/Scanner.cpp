#include <Scanner.h>
#include <AddressResolver.h>
#include <DeviceInfo.h>
#include <chrono>
#include <vector>
#include <DeviceData.h>
#include <Events.h>
#include <ConnectionManager.h>
#include <ThreadPool.h>
#include <DebugLog.h>

LanDeviceScanner* LanDeviceScanner::s_instance{nullptr};

LanDeviceScanner::LanDeviceScanner() : m_context(ThreadPool::GetContext()), m_strand(asio::make_strand(m_context)), m_outSocket(nullptr), m_inSocket(nullptr) { }

void LanDeviceScanner::EndScan() {
    if (s_instance == nullptr) {
        return;
    }

    if (!s_instance->m_isScanning.load()) {
        return;
    }

    asio::co_spawn(s_instance->m_strand, s_instance->Co_LeaveMulticastGroup(), asio::detached);
}

void LanDeviceScanner::BeginScan() {
    if (s_instance == nullptr) {
        s_instance = new LanDeviceScanner();
    }

    if (s_instance->m_isScanning.load()) {
        return;
    }

    s_instance->m_discoveredDevices.clear();
    s_instance->m_devicesLastProbe.clear();

    asio::co_spawn(s_instance->m_strand, s_instance->Co_JoinMulticastGroup(), asio::detached);
}

std::vector<DeviceInfo> LanDeviceScanner::GetDiscoveredDevices() {
    if (s_instance == nullptr) {
        s_instance = new LanDeviceScanner();
    }

    const size_t currentTime = GetTimeMS();
    constexpr size_t minimalLastProbe = 2500;

    std::lock_guard<std::mutex> lock(s_instance->m_mutex);

    std::erase_if(s_instance->m_discoveredDevices, [&](const DeviceInfo& deviceInfo) {
        if (currentTime - s_instance->m_devicesLastProbe[deviceInfo.deviceID] >= minimalLastProbe) {
            s_instance->m_devicesLastProbe.erase(deviceInfo.deviceID);
            return true;
        }

        return false;
    });

    return s_instance->m_discoveredDevices;
}

asio::awaitable<void> LanDeviceScanner::Co_JoinMulticastGroup() {
    try {
        co_await Co_LeaveMulticastGroup();
        const std::vector<IPAddress> addresses = AddressResolver::GetAllPrivateIPv4();

        m_isScanning.store(true);

        m_outSocket = std::make_unique<UDPSocket>(m_context);
        m_inSocket = std::make_unique<UDPSocket>(m_context);

        m_outSocket->open(asio::ip::udp::v4());
        m_outSocket->bind(asio::ip::udp::endpoint(asio::ip::address_v4::any(), 0));
        m_outSocket->set_option(asio::ip::multicast::enable_loopback(false));
        m_outSocket->set_option(asio::socket_base::reuse_address(true));
        m_outSocket->set_option(asio::ip::multicast::hops(8));

#ifdef SO_REUSEPORT
        int reuse = 1;
        ::setsockopt(m_outSocket->native_handle(), SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

        m_inSocket->open(asio::ip::udp::v4());
        m_inSocket->set_option(asio::socket_base::reuse_address(true));
        m_inSocket->set_option(asio::ip::multicast::enable_loopback(false));

#ifdef SO_REUSEPORT
int reuse = 1;
        ::setsockopt(m_inSocket->native_handle(), SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

        m_inSocket->bind(UDPEndpoint(asio::ip::address_v4::any(), DEVICE_DISCOVERY_MULTICAST_PORT));


        for (const auto& address : addresses) {
            if (address.is_loopback()) {
                continue;
            }

            m_inSocket->set_option(asio::ip::multicast::join_group(DEVICE_DISCOVERY_MULTICAST_ADDRESS, address.to_v4()));
        }

        asio::co_spawn(m_strand, Co_SendProbes(), asio::detached);
        asio::co_spawn(m_strand, Co_ReceiveResponses(), asio::detached);

    } catch (const std::system_error& errorCode) {
        ProcessError(errorCode);
    }

    co_return;
}

asio::awaitable<void> LanDeviceScanner::Co_LeaveMulticastGroup() {
    try {
        m_isScanning.store(false);

        if (m_inSocket != nullptr && m_inSocket->is_open()) {
            m_inSocket->cancel();
            m_inSocket->close();
            m_inSocket.reset();
        }

        if (m_outSocket != nullptr && m_outSocket->is_open()) {
            m_outSocket->cancel();
            m_outSocket->close();
            m_outSocket.reset();
        }

    } catch (const std::system_error& errorCode) {
        ProcessError(errorCode);
    }

    co_return;
}

asio::awaitable<void> LanDeviceScanner::Co_SendProbes() const {
    Debug::Log("LanDeviceScanner started sending probes");

    try {
        const UDPEndpoint multicastEndpoint(DEVICE_DISCOVERY_MULTICAST_ADDRESS, DEVICE_DISCOVERY_MULTICAST_PORT);

        do {
            std::vector<IPAddress> addresses = AddressResolver::GetAllPrivateIPv4();
            const DeviceInfo deviceInfo = DeviceInfo::GetThisDeviceInfo();

            std::vector<uint8_t> buffer;
            size_t offset = 0;

            buffer.resize(deviceInfo.GetSerializedSize());
            deviceInfo.Serialize(buffer, offset);

            const asio::const_buffer constBuffer = asio::const_buffer(buffer.data(), buffer.size());

            for (const auto& address : addresses) {
                m_outSocket->set_option(asio::ip::multicast::outbound_interface(address.to_v4()));
                co_await m_outSocket->async_send_to(constBuffer, multicastEndpoint, asio::use_awaitable);
            }

            asio::steady_timer timer(m_context);
            timer.expires_after(std::chrono::seconds(1));
            co_await timer.async_wait(asio::use_awaitable);

        } while (m_isScanning.load());

    } catch (const std::system_error& errorCode) {
        ProcessError(errorCode);
    }

    Debug::Log("LanDeviceScanner stopped sending probes");
}

asio::awaitable<void> LanDeviceScanner::Co_ReceiveResponses() {
    Debug::Log("LanDeviceScanner started receiving probes");

    try {
        DeviceInfo device = {};
        std::vector<uint8_t> buffer;
        buffer.resize(1024);

        do {
            asio::mutable_buffer mutableBuffer(buffer.data(), buffer.size());
            UDPEndpoint senderEndpoint;

            co_await m_inSocket->async_receive_from(mutableBuffer, senderEndpoint, asio::use_awaitable);

            std::size_t offset = 0;
            device.Deserialize(buffer, offset);
            device.deviceAddress = senderEndpoint.address().to_string();

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = std::ranges::find_if(m_discoveredDevices, [&](const auto& d) { return d.deviceID == device.deviceID; });
                if (it != m_discoveredDevices.end()) {
                    *it = device;
                } else {
                    m_discoveredDevices.push_back(device);
                }

                m_devicesLastProbe[device.deviceID] = GetTimeMS();
            }

        } while (m_isScanning.load());

    } catch (const std::system_error& errorCode) {
        ProcessError(errorCode);
    }

    Debug::Log("LanDeviceScanner stopped receiving probes");
}

void LanDeviceScanner::ProcessError(const asio::system_error& error) {
    HandleAsioError(error.code());
    const std::unique_ptr<QEvent> event = std::make_unique<ScannerErrorEvent>(error.code());
    ConnectionManager::SendEvent(event);
}

size_t LanDeviceScanner::GetTimeMS() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}


