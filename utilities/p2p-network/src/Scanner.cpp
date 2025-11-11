#include <Scanner.h>
#include <DebugLog.h>
#include <AddressResolver.h>
#include <Packable.h>
#include <chrono>
#include <vector>
#include <DeviceData.h>

LanDeviceScanner* LanDeviceScanner::s_instance{nullptr};

LanDeviceScanner::LanDeviceScanner() : m_awaitableFlag(m_context.get_executor()), m_workGuard(asio::make_work_guard(m_context.get_executor())) {
    m_contextThread = std::thread([this]() {
        m_context.run();
    });
}

void LanDeviceScanner::EndScan() {
    if (s_instance == nullptr) {
        return;
    }

    if (!s_instance->m_isScanning) {
        return;
    }

    asio::co_spawn(s_instance->m_context, s_instance->Co_LeaveMulticastGroup(), asio::detached);
}

void LanDeviceScanner::BeginScan() {
    if (s_instance == nullptr) {
        s_instance = new LanDeviceScanner();
    }

    if (s_instance->m_isScanning) {
        return;
    }

    s_instance->m_discoveredDevices.clear();
    s_instance->m_devicesLastProbe.clear();

    asio::co_spawn(s_instance->m_context, s_instance->Co_JoinMulticastGroup(), asio::detached);
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

        m_sendSockets.reserve(addresses.size());
        m_receiveSockets.reserve(addresses.size());
        int i = 0;

        while (m_jobsActive > 0) {
            asio::steady_timer timer(m_context);
            timer.expires_after(std::chrono::milliseconds(10));
            co_await timer.async_wait(asio::use_awaitable);
        }

        m_isScanning = true;

        for (const auto& address : addresses) {
            if (address.is_loopback()) {
                continue;
            }

            {
                UDPSocket socket(m_context);
                socket.open(asio::ip::udp::v4());
                socket.set_option(asio::ip::multicast::enable_loopback(false));
                socket.set_option(asio::socket_base::reuse_address(true));
#ifdef SO_REUSEPORT
                int reuse = 1;
                ::setsockopt(socket.native_handle(), SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif
                socket.bind(asio::ip::udp::endpoint(address.to_v4(), 0));
                socket.set_option(asio::ip::multicast::outbound_interface(address.to_v4()));
                m_sendSockets.push_back(std::move(socket));
            }

            {
                UDPSocket socket(m_context);
                socket.open(asio::ip::udp::v4());
                socket.set_option(asio::socket_base::reuse_address(true));

#ifdef SO_REUSEPORT
                int reuse = 1;
                ::setsockopt(socket.native_handle(), SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

                socket.set_option(asio::ip::multicast::enable_loopback(false));
                socket.bind(UDPEndpoint(asio::ip::address_v4::any(), DEVICE_DISCOVERY_MULTICAST_PORT));
                socket.set_option(asio::ip::multicast::join_group(DEVICE_DISCOVERY_MULTICAST_ADDRESS, address.to_v4()));
                m_receiveSockets.push_back(std::move(socket));
            }

            asio::co_spawn(m_context, Co_SendProbes(m_sendSockets[i]), asio::detached);
            asio::co_spawn(m_context, Co_ReceiveResponses(m_receiveSockets[i]), asio::detached);

            i++;
        }

    } catch (const std::system_error& errorCode) {
        Debug::LogError(errorCode.what());
    }

    co_return;
}

asio::awaitable<void> LanDeviceScanner::Co_LeaveMulticastGroup() {
    try {
        m_isScanning = false;

        for (auto& socket : m_sendSockets) {
            if (!socket.is_open()) {
                continue;
            }

            socket.cancel();
            socket.close();
        }

        for (auto& socket : m_receiveSockets) {
            if (!socket.is_open()) {
                continue;
            }

            socket.cancel();
            socket.close();
        }

        m_sendSockets.clear();
        m_receiveSockets.clear();

    } catch (const std::system_error& errorCode) {
        Debug::LogError(errorCode.what());
    }

    co_return;
}

asio::awaitable<void> LanDeviceScanner::Co_SendProbes(UDPSocket& socket) {
    m_jobsActive++;

    try {
        const UDPEndpoint multicastEndpoint(DEVICE_DISCOVERY_MULTICAST_ADDRESS, DEVICE_DISCOVERY_MULTICAST_PORT);

        do {
            const DeviceInfo deviceInfo = DeviceInfo::GetThisDeviceInfo();

            std::vector<uint8_t> buffer;
            size_t offset = 0;

            buffer.resize(deviceInfo.GetSerializedSize());
            deviceInfo.Serialize(buffer, offset);

            const asio::const_buffer constBuffer = asio::const_buffer(buffer.data(), buffer.size());
            co_await socket.async_send_to(constBuffer, multicastEndpoint, asio::use_awaitable);

            asio::steady_timer timer(m_context);
            timer.expires_after(std::chrono::seconds(1));
            co_await timer.async_wait(asio::use_awaitable);

        } while (m_isScanning);

    } catch (const std::system_error& errorCode) {
        Debug::LogError(errorCode.what());
    }

    m_jobsActive--;
}

asio::awaitable<void> LanDeviceScanner::Co_ReceiveResponses(UDPSocket& socket) {
    m_jobsActive++;

    try {
        DeviceInfo device = {};
        std::vector<uint8_t> buffer;
        buffer.resize(1024);

        do {
            asio::mutable_buffer mutableBuffer(buffer.data(), buffer.size());
            UDPEndpoint senderEndpoint;

            co_await socket.async_receive_from(mutableBuffer, senderEndpoint, asio::use_awaitable);

            Debug::Log("Received response from {}", senderEndpoint.address().to_string());

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

        } while (m_isScanning);

    } catch (const std::system_error& errorCode) {
        Debug::LogError(errorCode.what());
    }

    m_jobsActive--;
}

size_t LanDeviceScanner::GetTimeMS() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}


