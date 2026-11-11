#ifndef DAEMON_SERVER_H
#define DAEMON_SERVER_H

#include <AsioCommon.h>
#include <Packable.h>
#include <DaemonCommon.h>
#include <Package.h>
#include <vector>
#include <deque>
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
    decltype(GetPid()) m_pid{};
    std::deque<std::shared_ptr<std::vector<uint8_t>>> m_writes;
};

class DaemonServer : public std::enable_shared_from_this<DaemonServer> {
public:
    explicit DaemonServer();
    bool Start();
    std::vector<uuid> GetConnectedDevices();

    template <Serializable... Args>
    void Send(const std::shared_ptr<ClientData>& client, DaemonPackage type, Args... args) {
        if (!client) return;

        auto package = Package<DaemonPackage>::Create(type, args...);
        PackageHeader header = package.GetHeader();

        auto buffer = std::make_shared<std::vector<uint8_t>>(PackageHeader::GetSerializedSize() + header.size);
        size_t offset = 0;
        header.Serialize(*buffer, offset);
        if (header.size > 0) {
            std::memcpy(buffer->data() + offset, package.GetRawBody(), header.size);
        }
        asio::post(client->m_socket.get_executor(), [client, buffer]() {
            const bool writing = !client->m_writes.empty();
            client->m_writes.push_back(buffer);
            if (!writing) {
                WriteNext(client);
            }
        });
    }

private:
    static void WriteNext(const std::shared_ptr<ClientData>& client);
    asio::awaitable<void> SeekConnections();
    asio::awaitable<void> ProcessClient(std::shared_ptr<ClientData> client);

    std::mutex m_clientsMutex{};
    std::unique_ptr<TCPAcceptor> m_acceptor;
    std::vector<std::shared_ptr<ClientData>> m_clients{};
    std::vector<uuid> m_connectedDevices{};

};

#endif // DAEMON_SERVER_H
