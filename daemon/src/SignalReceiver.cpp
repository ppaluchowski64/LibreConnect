#include <SignalReceiver.h>
#include <asio.hpp>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <cerrno>
#endif

SignalReceiver* SignalReceiver::s_instance{nullptr};
std::mutex SignalReceiver::s_mutex{};

void SignalReceiver::StartReceiving() {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_instance) {
        s_instance = new SignalReceiver();
    }

    asio::post(s_instance->m_strand, []() {
        if (s_instance->m_isRunning) {
            return;
        }

        s_instance->m_socket = std::make_unique<UDPSocket>(ThreadPool::GetContext());
        s_instance->m_socket->bind(UDPEndpoint(asio::ip::make_address_v4("127.0.0.1"), DAEMON_SIGNAL_PORT));

        asio::co_spawn(s_instance->m_strand, s_instance->CoReceive(), asio::detached);
    });
}

void SignalReceiver::StopReceiving() {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_instance) return;

    asio::post(s_instance->m_strand, []() {
        if (!s_instance->m_isRunning) {
            return;
        }

        s_instance->m_isRunning = false;
        s_instance->m_connectedDevices = {};

        s_instance->m_socket->shutdown(UDPSocket::shutdown_both);
        s_instance->m_socket->close();
        s_instance->m_socket.reset();
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

    while (m_isRunning) {
        co_await m_socket->async_receive(buffer);

        {
            std::lock_guard<std::mutex> lock(s_mutex);

            if (payload.newConnection) {
                m_connectedDevices.push_back(payload.target);
            } else {
                std::erase_if(m_connectedDevices, [target = payload.target](const Device& device) {
                    return target.m_uuid == device.m_uuid;
                });
            }
        }
    }
}
