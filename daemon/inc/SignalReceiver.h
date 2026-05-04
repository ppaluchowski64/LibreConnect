#pragma once
#include <AsioCommon.h>
#include <Packable.h>
#include <ThreadPool.h>

static auto GetPid() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return getpid();
#endif
}

class SignalReceiver {
public:
    static void StartReceiving();
    static void StopReceiving();
    static std::vector<uuid> GetConnectedDevices();

private:
    struct Device {
        uuid m_uuid;
        decltype(GetPid()) m_pid;
    };

    static SignalReceiver* s_instance;
    static std::mutex s_mutex;

    static bool IsProcessAlive(auto pid);
    asio::awaitable<void> CoReceive();

    IOContextStrand m_strand{ThreadPool::GetContext().get_executor()};
    std::unique_ptr<UDPSocket> m_socket{nullptr};
    std::vector<Device> m_connectedDevices{};
    bool m_isRunning{false};

};
