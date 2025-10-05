#include <Scanner.h>
#include <DebugLog.h>
#include <AddressResolver.h>
#include <Package.h>
#include <coroutine>

LanDeviceScanner* LanDeviceScanner::s_instance{nullptr};

LanDeviceScanner::LanDeviceScanner() : m_awaitableFlag(m_context.get_executor()), m_workGuard(asio::make_work_guard(m_context.get_executor())), m_socket(m_context) {
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

    asio::co_spawn(s_instance->m_context, s_instance->Co_JoinMulticastGroup(), asio::detached);
}

std::vector<DeviceInfo> LanDeviceScanner::GetDiscoveredDevices() {
    if (s_instance == nullptr) {
        s_instance = new LanDeviceScanner();
    }

    return s_instance->m_discoveredDevices;
}

asio::awaitable<void> LanDeviceScanner::Co_JoinMulticastGroup() {
    try {
        const std::vector<IPAddress> addresses = AddressResolver::GetAllPrivateIPv4();

        m_socket.open(asio::ip::udp::v4());
        m_socket.set_option(asio::socket_base::reuse_address(true));
        m_socket.bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), DEVICE_DISCOVERY_MULTICAST_PORT));
        m_socket.set_option(asio::ip::multicast::enable_loopback(false));

        for (const auto& address : addresses) {
            m_socket.set_option(asio::ip::multicast::join_group(DEVICE_DISCOVERY_MULTICAST_ADDRESS, address.to_v4()));
        }

        m_isScanning = true;

        asio::co_spawn(m_context, Co_SendProbes(), asio::detached);
        asio::co_spawn(m_context, Co_ReceiveResponses(), asio::detached);

    } catch (const std::system_error& errorCode) {
        Debug::LogError(errorCode.what());
    }

    co_return;
}

asio::awaitable<void> LanDeviceScanner::Co_LeaveMulticastGroup() {
    try {
        m_socket.set_option(asio::ip::multicast::leave_group(DEVICE_DISCOVERY_MULTICAST_ADDRESS));
        m_socket.cancel();
        m_socket.close();

        m_isScanning = false;

    } catch (const std::system_error& errorCode) {
        Debug::LogError(errorCode.what());
    }

    co_return;
}

asio::awaitable<void> LanDeviceScanner::Co_SendProbes() {
    try {
        const UDPEndpoint multicastEndpoint(DEVICE_DISCOVERY_MULTICAST_ADDRESS, DEVICE_DISCOVERY_MULTICAST_PORT);
        const std::vector<IPAddress> addresses = AddressResolver::GetAllPrivateIPv6();
        const DeviceInfo deviceInfo = GetDeviceInfo();

        std::vector<uint8_t> buffer;
        size_t offset = 0;

        buffer.resize(deviceInfo.GetSerializedSize());
        deviceInfo.Serialize(buffer, offset);
        const asio::const_buffer constBuffer = asio::const_buffer(buffer.data(), buffer.size());

        do {
            for (const auto& address : addresses) {
                m_socket.set_option(asio::ip::multicast::outbound_interface(address.to_v4()));
                co_await m_socket.async_send_to(constBuffer, multicastEndpoint, asio::use_awaitable);
            }

            asio::steady_timer timer(m_context);
            timer.expires_after(std::chrono::seconds(1));
            co_await timer.async_wait(asio::use_awaitable);

        } while (m_isScanning);

    } catch (const std::system_error& errorCode) {
        Debug::LogError(errorCode.what());
    }
}

asio::awaitable<void> LanDeviceScanner::Co_ReceiveResponses() {
    try {
        DeviceInfo device = {};
        std::vector<uint8_t> buffer;
        buffer.resize(1024);

        do {
            asio::mutable_buffer mutableBuffer(buffer.data(), buffer.size());
            UDPEndpoint senderEndpoint;

            co_await m_socket.async_receive_from(mutableBuffer, senderEndpoint, asio::use_awaitable);

            std::size_t offset = 0;
            device.Deserialize(buffer, offset);

            Debug::Log("Device, name: {}, endpoint: {}", device.deviceName, senderEndpoint.address().to_string());

        } while (m_isScanning);

    } catch (const std::system_error& errorCode) {
        Debug::LogError(errorCode.what());
    }
}

DeviceInfo LanDeviceScanner::GetDeviceInfo() {
    DeviceInfo device = {
        asio::ip::host_name()
    };

    return device;
}


