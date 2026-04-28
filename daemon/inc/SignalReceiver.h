#pragma once
#include <AsioCommon.h>
#include <Packable.h>
#include <ThreadPool.h>

class SignalReceiver {
public:
    static void StartReceiving();
    static void StopReceiving();
    static std::vector<uuid> GetConnectedDevices();

private:
    struct Device {
        uuid m_uuid;
        int m_pid;
    };

    static SignalReceiver* s_instance;
    static std::mutex s_mutex;

    static bool IsProcessAlive(int pid);
    asio::awaitable<void> CoReceive();

    IOContextStrand m_strand{ThreadPool::GetContext().get_executor()};
    std::unique_ptr<UDPSocket> m_socket{nullptr};
    std::vector<Device> m_connectedDevices{};
    bool m_isRunning{false};

};
