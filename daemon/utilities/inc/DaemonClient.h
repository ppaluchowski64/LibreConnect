#ifndef DAEMON_CLIENT_H
#define DAEMON_CLIENT_H

#include <AsioCommon.h>
#include <DaemonCommon.h>
#include <Package.h>
#include <memory>
#include <atomic>
#include <chrono>
#include <deque>
#include <AwaitableFlag.h>
#include <ConcurrentUnorderedMap.h>

class DaemonClient : public std::enable_shared_from_this<DaemonClient> {
public:
    explicit DaemonClient();
    static std::shared_ptr<DaemonClient> Create();
    static void Destroy(const std::shared_ptr<DaemonClient>& client);

    bool IsConnected() const;
    bool HasFinishedConnectAttempt() const;
    asio::awaitable<bool> WaitForConnectResult(std::chrono::milliseconds timeout);
    void ConnectedSignal(uuid uuid);
    asio::awaitable<bool> RequestConnectedWindow(uuid uuid);

private:
    template <Serializable... Args>
    void Send(DaemonPackage type, Args... args) {
        auto package = Package<DaemonPackage>::Create(type, args...);
        PackageHeader header = package.GetHeader();

        auto buffer = std::make_shared<std::vector<uint8_t>>(PackageHeader::GetSerializedSize() + header.size);
        size_t offset = 0;
        header.Serialize(*buffer, offset);
        if (header.size > 0) {
            std::memcpy(buffer->data() + offset, package.GetRawBody(), header.size);
        }
        const std::shared_ptr<DaemonClient> self = shared_from_this();
        asio::post(m_socket.get_executor(), [self, buffer]() {
            if (!self->m_connected.load()) {
                return;
            }
            const bool writing = !self->m_writes.empty();
            self->m_writes.push_back(buffer);
            if (!writing) {
                self->WriteNext();
            }
        });
    }


    asio::awaitable<void> CoConnect();
    asio::awaitable<void> CoReceive();
    asio::awaitable<bool> CoRequestConnectedWindow(uuid uuid);
    void WriteNext();

    TCPSocket m_socket;
    asio::steady_timer m_retryTimer;
    std::deque<std::shared_ptr<std::vector<uint8_t>>> m_writes;
    uuid m_deviceId{};
    std::atomic_bool m_destroyed{false};
    AwaitableFlag m_connectedFlag;
    std::atomic_bool m_connected{false};
    std::atomic_bool m_connectAttemptFinished{false};
    ConcurrentUnorderedMap<uuid, std::shared_ptr<AwaitableFlag>> m_windowRequestFlags;
    ConcurrentUnorderedMap<uuid, bool> m_windowRequestResults;
};

#endif //DAEMON_CLIENT_H
