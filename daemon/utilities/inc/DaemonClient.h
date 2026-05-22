#ifndef DAEMON_CLIENT_H
#define DAEMON_CLIENT_H

#include <AsioCommon.h>
#include <DaemonCommon.h>
#include <Package.h>
#include <memory>
#include <atomic>
#include <AwaitableFlag.h>
#include <ConcurrentUnorderedMap.h>

class DaemonClient : public std::enable_shared_from_this<DaemonClient> {
public:
    explicit DaemonClient();
    static std::shared_ptr<DaemonClient> Create();
    static void Destroy(const std::shared_ptr<DaemonClient>& client);

    bool IsConnected() const;
    void ConnectedSignal(uuid uuid);
    asio::awaitable<bool> RequestConnectedWindow(uuid uuid);

private:
    template <Serializable... Args>
    void Send(DaemonPackage type, Args... args) {
        auto package = Package<DaemonPackage>::Create(type, args...);
        PackageHeader header = package.GetHeader();

        auto headerBuffer = std::make_shared<std::vector<uint8_t>>(PackageHeader::GetSerializedSize());
        size_t offset = 0;
        header.Serialize(*headerBuffer, offset);

        auto bodyBuffer = std::make_shared<std::vector<uint8_t>>(header.size);
        std::memcpy(bodyBuffer->data(), package.GetRawBody(), header.size);

        std::vector<asio::const_buffer> buffers {
            asio::buffer(*headerBuffer),
            asio::buffer(*bodyBuffer)
        };

        asio::async_write(m_socket, buffers, [headerBuffer, bodyBuffer](const std::error_code&, std::size_t) {});
    }


    asio::awaitable<void> CoConnect();
    asio::awaitable<void> CoReceive();

    TCPSocket m_socket;
    std::atomic_bool m_connected{false};
    ConcurrentUnorderedMap<uuid, std::shared_ptr<AwaitableFlag>> m_windowRequestFlags;
    ConcurrentUnorderedMap<uuid, bool> m_windowRequestResults;
};

#endif //DAEMON_CLIENT_H
