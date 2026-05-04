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

class SignalSender {
public:
    SignalSender();

    void ConnectionSignal(uuid id);
    void DisconnectionSignal(uuid id);

private:
    struct Device {
        uuid m_uuid;
        decltype(GetPid()) m_pid;
    };

#pragma pack(push, 1)
    struct Payload {
        bool newConnection;
        Device target;
    };
#pragma pack(pop)

    asio::awaitable<void> CoSendPayload(Payload payload);

    IOContextStrand m_strand{ThreadPool::GetContext().get_executor()};
    UDPSocket m_socket{ThreadPool::GetContext()};

};