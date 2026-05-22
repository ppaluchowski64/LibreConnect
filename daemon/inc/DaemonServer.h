#ifndef DAEMON_SERVER_H
#define DAEMON_SERVER_H

#include <AsioCommon.h>
#include <Packable.h>
#include <DaemonCommon.h>
#include <Package.h>
#include <vector>
#include <boost/uuid.hpp>

static auto GetPid() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return getpid();
#endif
}

struct ClientData {
    TCPSocket m_socket;
    uuid m_uuid;
    decltype(GetPid()) m_pid;
};

class DaemonServer : public std::enable_shared_from_this<DaemonServer> {
public:
    explicit DaemonServer();
    void Start();
    std::vector<uuid> GetConnectedDevices();

    template <Serializable... Args>
    void Send(const std::shared_ptr<ClientData>& client, DaemonPackage type, Args... args) {
        if (!client) return;

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

        asio::async_write(client->m_socket, buffers, [headerBuffer, bodyBuffer](const std::error_code&, std::size_t) {});
    }

private:
    asio::awaitable<void> SeekConnections();
    asio::awaitable<void> ProcessClient(std::shared_ptr<ClientData> client);

    std::mutex m_clientsMutex{};
    std::vector<std::shared_ptr<ClientData>> m_clients{};
    std::vector<uuid> m_connectedDevices{};

};

#endif // DAEMON_SERVER_H
