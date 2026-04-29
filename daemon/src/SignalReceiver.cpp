#include <SignalReceiver.h>
#include <asio.hpp>
#include <boost/uuid/uuid_io.hpp>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <cerrno>
#endif

namespace
{
bool IsBenignSignalReceiverShutdownError(const std::error_code& errorCode)
{
    return errorCode == asio::error::operation_aborted ||
           errorCode == asio::error::not_connected ||
           errorCode == asio::error::bad_descriptor;
}
}

SignalReceiver* SignalReceiver::s_instance{nullptr};
std::mutex SignalReceiver::s_mutex{};

void SignalReceiver::StartReceiving() {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_instance) {
        s_instance = new SignalReceiver();
        Debug::Log("SignalReceiver::StartReceiving created receiver instance");
    }

    Debug::Log("SignalReceiver::StartReceiving requested");
    asio::post(s_instance->m_strand, [instance = s_instance]() {
        try {
            if (instance->m_isRunning) {
                Debug::Log("SignalReceiver::StartReceiving ignored: receiver already active");
                return;
            }

            instance->m_socket = std::make_unique<UDPSocket>(ThreadPool::GetContext());
            instance->m_socket->open(asio::ip::udp::v4());
            instance->m_socket->bind(UDPEndpoint(asio::ip::make_address_v4("127.0.0.1"), DAEMON_SIGNAL_PORT));
            instance->m_isRunning = true;
            Debug::Log("SignalReceiver::StartReceiving bound UDP socket on 127.0.0.1:{}", DAEMON_SIGNAL_PORT);

            asio::co_spawn(instance->m_strand, instance->CoReceive(), asio::detached);
        } catch (const std::system_error& errorCode) {
            instance->m_isRunning = false;
            instance->m_socket.reset();
            Debug::LogError("SignalReceiver::StartReceiving failed: {}", errorCode.what());
            HandleAsioError(errorCode.code());
        }
    });
}

void SignalReceiver::StopReceiving() {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_instance) {
        Debug::Log("SignalReceiver::StopReceiving ignored: receiver instance is null");
        return;
    }

    if (!s_instance->m_isRunning) {
        Debug::Log("SignalReceiver::StopReceiving ignored: receiver is not active");
        return;
    }

    Debug::Log("SignalReceiver::StopReceiving requested");
    asio::post(s_instance->m_strand, [instance = s_instance]() {
        try {
            if (!instance->m_isRunning) {
                Debug::Log("SignalReceiver::StopReceiving ignored on strand: receiver is not active");
                return;
            }

            instance->m_isRunning = false;
            instance->m_connectedDevices.clear();

            if (instance->m_socket != nullptr && instance->m_socket->is_open()) {
                instance->m_socket->cancel();
                instance->m_socket->close();
                instance->m_socket.reset();
                Debug::Log("SignalReceiver::StopReceiving input socket closed");
            }
        } catch (const std::system_error& errorCode) {
            Debug::LogError("SignalReceiver::StopReceiving failed: {}", errorCode.what());
            HandleAsioError(errorCode.code());
        }
    });
}

std::vector<uuid> SignalReceiver::GetConnectedDevices() {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_instance) {
        return {};
    }

    std::erase_if(s_instance->m_connectedDevices, [](const Device& device) {
        return !IsProcessAlive(device.m_pid);
    });

    std::vector<uuid> connectedDevices;
    connectedDevices.resize(s_instance->m_connectedDevices.size());

    for (size_t i = 0; i < s_instance->m_connectedDevices.size(); ++i) {
        connectedDevices[i] = s_instance->m_connectedDevices[i].m_uuid;
    }

    return connectedDevices;
}

bool SignalReceiver::IsProcessAlive(const int pid) {
#ifdef _WIN32

    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);

    if (!process) {
        if (GetLastError() == ERROR_ACCESS_DENIED) {
            return true;
        }
        return false;
    }

    DWORD exitCode = 0;
    if (GetExitCodeProcess(process, &exitCode)) {
        CloseHandle(process);
        return exitCode == STILL_ACTIVE;
    }

    CloseHandle(process);
    return false;

#else

    if (kill(pid, 0) == 0) {
        return true;
    }

    if (errno == EPERM) {
        return true;
    }

    return false;

#endif
}

asio::awaitable<void> SignalReceiver::CoReceive() {
#pragma pack(push, 1)
    struct IncomingPayload {
        bool newConnection;
        Device target;
    };
#pragma pack(pop)

    IncomingPayload payload{};
    const asio::mutable_buffer buffer(&payload, sizeof(payload));
    Debug::Log("SignalReceiver started receiving daemon signals");

    try {
        while (m_isRunning) {
            co_await m_socket->async_receive(buffer);

            {
                std::lock_guard<std::mutex> lock(s_mutex);

                if (payload.newConnection) {
                    m_connectedDevices.push_back(payload.target);
                    Debug::Log(
                        "SignalReceiver::CoReceive registered connected device {} (pid: {})",
                        boost::uuids::to_string(payload.target.m_uuid),
                        payload.target.m_pid
                    );
                } else {
                    const size_t previousCount = m_connectedDevices.size();
                    std::erase_if(m_connectedDevices, [target = payload.target](const Device& device) {
                        return target.m_uuid == device.m_uuid;
                    });

                    const size_t removed = previousCount - m_connectedDevices.size();
                    if (removed > 0) {
                        Debug::Log(
                            "SignalReceiver::CoReceive removed connected device {} (remaining: {})",
                            boost::uuids::to_string(payload.target.m_uuid),
                            m_connectedDevices.size()
                        );
                    }
                }
            }
        }
    } catch (const std::system_error& errorCode) {
        if (m_isRunning || !IsBenignSignalReceiverShutdownError(errorCode.code())) {
            HandleAsioError(errorCode.code());
        }
    }

    Debug::Log("SignalReceiver stopped receiving daemon signals");
}
