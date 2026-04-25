#include <Scanner.h>
#include <AddressResolver.h>
#include <DeviceInfo.h>
#include <chrono>
#include <vector>
#include <Events.h>
#include <ConnectionManager.h>
#include <ThreadPool.h>
#include <DebugLog.h>
#include <QNetworkInformation>

namespace
{
bool MatchesErrorCondition(const std::error_code& errorCode, const std::errc condition)
{
    return errorCode == std::make_error_code(condition) ||
           errorCode.default_error_condition() == std::make_error_condition(condition);
}

bool IsTransientScannerNetworkError(const std::error_code& errorCode)
{
    return MatchesErrorCondition(errorCode, std::errc::host_unreachable) ||
           MatchesErrorCondition(errorCode, std::errc::network_unreachable) ||
           MatchesErrorCondition(errorCode, std::errc::network_down) ||
           MatchesErrorCondition(errorCode, std::errc::address_not_available);
}
}

LanDeviceScanner* LanDeviceScanner::s_instance{nullptr};

LanDeviceScanner::LanDeviceScanner() : m_context(ThreadPool::GetContext()), m_strand(asio::make_strand(m_context)), m_outSocket(nullptr), m_inSocket(nullptr) {
    const auto netInstance = QNetworkInformation::instance();
    QObject::connect(netInstance, &QNetworkInformation::transportMediumChanged, [this]() {
        if (!m_isScanning.load()) {
            return;
        }

        Debug::Log("LanDeviceScanner: transport medium changed, restarting scan");
        RestartScan();
    });
}

void LanDeviceScanner::EndScan() {
    if (s_instance == nullptr) {
        Debug::Log("LanDeviceScanner::EndScan ignored: scanner instance is null");
        return;
    }

    if (!s_instance->m_isScanning.load()) {
        Debug::Log("LanDeviceScanner::EndScan ignored: scanner is not active");
        return;
    }

    Debug::Log("LanDeviceScanner::EndScan requested");
    asio::co_spawn(s_instance->m_strand, s_instance->Co_LeaveMulticastGroup(), asio::detached);
}

void LanDeviceScanner::RestartScan() {
    if (s_instance == nullptr) {
        Debug::Log("LanDeviceScanner::RestartScan ignored: scanner instance is null");
        return;
    }

    Debug::Log("LanDeviceScanner::RestartScan requested");
    asio::co_spawn(s_instance->m_strand, s_instance->Co_RestartScan(), asio::detached);
}

void LanDeviceScanner::BeginScan() {
    if (s_instance == nullptr) {
        s_instance = new LanDeviceScanner();
        Debug::Log("LanDeviceScanner::BeginScan created scanner instance");
    }

    if (s_instance->m_isScanning.load()) {
        Debug::Log("LanDeviceScanner::BeginScan ignored: scanner already active");
        return;
    }

    s_instance->m_discoveredDevices.clear();
    s_instance->m_devicesLastProbe.clear();
    Debug::Log("LanDeviceScanner::BeginScan started");

    asio::co_spawn(s_instance->m_strand, s_instance->Co_JoinMulticastGroup(), asio::detached);
}

std::vector<DeviceInfo> LanDeviceScanner::GetDiscoveredDevices() {
    if (s_instance == nullptr) {
        s_instance = new LanDeviceScanner();
        Debug::Log("LanDeviceScanner::GetDiscoveredDevices created scanner instance");
    }

    const size_t currentTime = GetTimeMS();
    constexpr size_t minimalLastProbe = 2500;

    std::lock_guard<std::mutex> lock(s_instance->m_mutex);
    const size_t previousCount = s_instance->m_discoveredDevices.size();

    std::erase_if(s_instance->m_discoveredDevices, [&](const DeviceInfo& deviceInfo) {
        if (currentTime - s_instance->m_devicesLastProbe[deviceInfo.deviceID] >= minimalLastProbe) {
            s_instance->m_devicesLastProbe.erase(deviceInfo.deviceID);
            return true;
        }

        return false;
    });

    const size_t removed = previousCount - s_instance->m_discoveredDevices.size();
    if (removed > 0) {
        Debug::Log("LanDeviceScanner::GetDiscoveredDevices removed stale devices: {} (remaining: {})", removed, s_instance->m_discoveredDevices.size());
    }

    return s_instance->m_discoveredDevices;
}

asio::awaitable<void> LanDeviceScanner::Co_RestartScan() const {
    if (!m_isScanning.load()) {
        Debug::Log("LanDeviceScanner::Co_RestartScan scanner not active, starting new scan");
        BeginScan();
        co_return;
    }

    Debug::Log("LanDeviceScanner::Co_RestartScan restarting active scan");
    co_await s_instance->Co_LeaveMulticastGroup();
    BeginScan();
}

asio::awaitable<void> LanDeviceScanner::Co_JoinMulticastGroup() {
    try {
        co_await Co_LeaveMulticastGroup();
        const std::vector<IPAddress> addresses = AddressResolver::GetAllPrivateIPv4();
        Debug::Log("LanDeviceScanner::Co_JoinMulticastGroup private IPv4 interfaces: {}", addresses.size());

        m_isScanning.store(true);

        m_outSocket = std::make_unique<UDPSocket>(m_context);
        m_inSocket = std::make_unique<UDPSocket>(m_context);

        m_outSocket->open(asio::ip::udp::v4());
        m_outSocket->bind(asio::ip::udp::endpoint(asio::ip::address_v4::any(), 0));
        m_outSocket->set_option(asio::ip::multicast::enable_loopback(false));
        m_outSocket->set_option(asio::socket_base::reuse_address(true));
        m_outSocket->set_option(asio::ip::multicast::hops(8));

        m_inSocket->open(asio::ip::udp::v4());
        m_inSocket->set_option(asio::socket_base::reuse_address(true));
        m_inSocket->set_option(asio::ip::multicast::enable_loopback(false));

#ifdef SO_REUSEPORT
        int reuse = 1;
        ::setsockopt(m_outSocket->native_handle(), SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
        ::setsockopt(m_inSocket->native_handle(), SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

        m_inSocket->bind(UDPEndpoint(asio::ip::address_v4::any(), DEVICE_DISCOVERY_MULTICAST_PORT));


        for (const auto& address : addresses) {
            if (address.is_loopback()) {
                Debug::Log("LanDeviceScanner::Co_JoinMulticastGroup skipping loopback interface: {}", address.to_string());
                continue;
            }

            Debug::Log("LanDeviceScanner::Co_JoinMulticastGroup joining multicast on {}", address.to_string());
            try {
                m_inSocket->set_option(asio::ip::multicast::join_group(DEVICE_DISCOVERY_MULTICAST_ADDRESS, address.to_v4()));
            } catch (const std::system_error& joinError) {
                if (joinError.code() == asio::error::address_in_use) {
                    Debug::LogWarning(
                        "LanDeviceScanner::Co_JoinMulticastGroup skipping duplicate multicast registration on {} ({})",
                        address.to_string(),
                        joinError.what()
                    );
                    continue;
                }

                if (IsTransientScannerNetworkError(joinError.code())) {
                    Debug::LogWarning(
                        "LanDeviceScanner::Co_JoinMulticastGroup skipping interface {} until it becomes routable ({})",
                        address.to_string(),
                        joinError.what()
                    );
                    continue;
                }

                throw;
            }
        }

        asio::co_spawn(m_strand, Co_SendProbes(), asio::detached);
        asio::co_spawn(m_strand, Co_ReceiveResponses(), asio::detached);
        Debug::Log("LanDeviceScanner::Co_JoinMulticastGroup joined and probe tasks started");

    } catch (const std::system_error& errorCode) {
        Debug::LogError("LanDeviceScanner::Co_JoinMulticastGroup failed: {}", errorCode.what());
        ProcessError(errorCode);
    }

    co_return;
}

asio::awaitable<void> LanDeviceScanner::Co_LeaveMulticastGroup() {
    try {
        Debug::Log("LanDeviceScanner::Co_LeaveMulticastGroup stopping scan");
        m_isScanning.store(false);

        if (m_inSocket != nullptr && m_inSocket->is_open()) {
            m_inSocket->cancel();
            m_inSocket->close();
            m_inSocket.reset();
            Debug::Log("LanDeviceScanner::Co_LeaveMulticastGroup input socket closed");
        }

        if (m_outSocket != nullptr && m_outSocket->is_open()) {
            m_outSocket->cancel();
            m_outSocket->close();
            m_outSocket.reset();
            Debug::Log("LanDeviceScanner::Co_LeaveMulticastGroup output socket closed");
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
                if (address.is_loopback()) {
                    continue;
                }

                std::error_code setOptionError;
                m_outSocket->set_option(asio::ip::multicast::outbound_interface(address.to_v4()), setOptionError);
                if (setOptionError) {
                    if (IsTransientScannerNetworkError(setOptionError)) {
                        Debug::LogWarning(
                            "LanDeviceScanner::Co_SendProbes skipping unroutable interface {} ({})",
                            address.to_string(),
                            setOptionError.message()
                        );
                        continue;
                    }

                    throw std::system_error(setOptionError);
                }

                std::error_code sendError;
                co_await m_outSocket->async_send_to(
                    constBuffer,
                    multicastEndpoint,
                    asio::redirect_error(asio::use_awaitable, sendError)
                );
                if (sendError) {
                    if (IsTransientScannerNetworkError(sendError)) {
                        Debug::LogWarning(
                            "LanDeviceScanner::Co_SendProbes send failed on {} but will retry ({})",
                            address.to_string(),
                            sendError.message()
                        );
                        continue;
                    }

                    throw std::system_error(sendError);
                }
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
    constexpr size_t RECEIVE_BUFFER_SIZE_ = 1024;

    Debug::Log("LanDeviceScanner started receiving probes");

    try {
        DeviceInfo device = {};
        std::vector<uint8_t> buffer;
        buffer.reserve(RECEIVE_BUFFER_SIZE_);

        do {
            buffer.resize(RECEIVE_BUFFER_SIZE_);
            asio::mutable_buffer mutableBuffer(buffer.data(), buffer.size());
            UDPEndpoint senderEndpoint;

            size_t receivedBytes = co_await m_inSocket->async_receive_from(mutableBuffer, senderEndpoint, asio::use_awaitable);
            buffer.resize(receivedBytes);

            std::size_t offset = 0;

            try {
                device.Deserialize(buffer, offset);
            } catch (...) {
                continue;
            }

            device.deviceAddress = senderEndpoint.address().to_string();

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = std::ranges::find_if(m_discoveredDevices, [&](const auto& d) { return d.deviceID == device.deviceID; });
                if (it != m_discoveredDevices.end()) {
                    *it = device;
                } else {
                    m_discoveredDevices.push_back(device);
                    Debug::Log("LanDeviceScanner::Co_ReceiveResponses discovered new device {} ({})", boost::uuids::to_string(device.deviceID), device.deviceAddress);
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
