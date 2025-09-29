#include <Scanner.h>
#include <DebugLog.h>
#include <AddressResolver.h>
#include <Package.h>
#include <coroutine>

LanDeviceScanner* LanDeviceScanner::s_instance{nullptr};

LanDeviceScanner::LanDeviceScanner() : m_awaitableFlag(m_context.get_executor()), m_workGuard(asio::make_work_guard(m_context.get_executor())), m_senderSocket(m_context), m_receiverSocket(m_context) {
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

        m_receiverSocket.open(asio::ip::udp::v4());
        m_receiverSocket.set_option(asio::socket_base::reuse_address(true));
        m_receiverSocket.bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), DEVICE_DISCOVERY_MULTICAST_PORT));

        for (const auto& address : addresses) {
            m_receiverSocket.set_option(asio::ip::multicast::join_group(DEVICE_DISCOVERY_MULTICAST_ADDRESS, address.to_v4()));
        }

        m_receiverSocket.set_option(asio::ip::multicast::enable_loopback(false));

        m_senderSocket.open(asio::ip::udp::v4());
        m_senderSocket.set_option(asio::ip::multicast::enable_loopback(false));

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
        m_receiverSocket.set_option(asio::ip::multicast::leave_group(DEVICE_DISCOVERY_MULTICAST_ADDRESS));

        m_receiverSocket.cancel();
        m_senderSocket.cancel();

        m_receiverSocket.close();
        m_senderSocket.close();

        m_isScanning = false;

    } catch (const std::system_error& errorCode) {
        Debug::LogError(errorCode.what());
    }

    co_return;
}

asio::awaitable<void> LanDeviceScanner::Co_SendProbes() {
    try {
        const std::vector<IPAddress> addresses = AddressResolver::GetAllPrivateIPv4();
        auto [deviceName] = GetDeviceInfo();

        Package<DeviceScannerPackageType> package = Package<DeviceScannerPackageType>::Create(DeviceScannerPackageType::None, std::move(deviceName));
        const asio::const_buffer constBuffer(package.GetRawBody(), package.GetHeader().size);
        const UDPEndpoint multicastEndpoint(DEVICE_DISCOVERY_MULTICAST_ADDRESS, DEVICE_DISCOVERY_MULTICAST_PORT);

        do {
            for (const auto& address : addresses) {
                m_senderSocket.set_option(asio::ip::multicast::outbound_interface(address.to_v4()));
                co_await m_senderSocket.async_send_to(constBuffer, multicastEndpoint, asio::use_awaitable);
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

        do {
            Package<DeviceScannerPackageType> package(PackageHeader{0, 1024, 0});
            asio::mutable_buffer buffer(package.GetRawBody(), package.GetHeader().size);
            UDPEndpoint senderEndpoint;

            // IGNORE THIS ERROR
            std::size_t bytesReceived = co_await m_receiverSocket.async_receive_from(buffer, senderEndpoint, asio::use_awaitable);
            package.GetValue<std::string>(device.deviceName);

            Debug::Log("Device, name: {}", device.deviceName);

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


