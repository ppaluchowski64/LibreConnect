#ifndef INITIAL_CONNECTION_H
#define INITIAL_CONNECTION_H

#include <asio.hpp>
#include <Package.h>
#include <Packable.h>
#include <AsioCommon.h>
#include <asio/awaitable.hpp>
#include <AwaitableFlag.h>
#include <deque>
#include <DeviceInfo.h>
#include <functional>

enum class InitialConnectionMode : uint8_t {
    PAIR_AND_CONNECT,
    CONNECT_WITH_PAIR,
    CONNECTION_WITHOUT_PAIR
};

enum class InitialConnectionPackageType : PackageTypeInt {
    CONNECT_INFO
};

struct InitialConnectionData {
    DeviceInfo deviceInfo;
    InitialConnectionMode initialConnectionMode;

    void Serialize(std::vector<uint8_t>& buffer, size_t& offset) const {
        deviceInfo.Serialize(buffer, offset);
        SerializeObject(initialConnectionMode, buffer, offset);
    }

    void Deserialize(const std::vector<uint8_t>& buffer, size_t& offset) {
        deviceInfo.Deserialize(buffer, offset);
        DeserializeObject(initialConnectionMode, buffer, offset);
    }

    inline size_t GetSerializedSize() const {
        return deviceInfo.GetSerializedSize() + GetObjectSerializedSize(initialConnectionMode);
    }
};

class ConnectionManager;

class InitialConnection final : public std::enable_shared_from_this<InitialConnection> {
public:
    explicit InitialConnection(IOContext& context);
    InitialConnection() = delete;

    static std::shared_ptr<InitialConnection> Create(IOContext& context);

    void Connect(TCPEndpoint&& endpoint, InitialConnectionMode mode);
    void Seek(TCPEndpoint&& endpoint, std::function<void(TCPEndpoint endpoint)>&& callback);
    void Disconnect(bool cancelSeeking = false);

    void TemporaryOwnership(std::shared_ptr<InitialConnection> ptr);

private:
    asio::awaitable<void> CoConnect(TCPEndpoint endpoint, InitialConnectionMode mode);
    asio::awaitable<void> CoSeek(TCPEndpoint endpoint, std::function<void(TCPEndpoint endpoint)> callback);
    asio::awaitable<void> CoDisconnect(bool cancelSeeking);
    asio::awaitable<void> CoSend();
    asio::awaitable<void> CoReceive();

    IOContext& m_context;
    asio::strand<asio::io_context::executor_type> m_strand;

    AwaitableFlag m_sendFlag;
    TCPSocket m_socket;

    std::deque<std::unique_ptr<Package<InitialConnectionPackageType>>> m_packagesOut;
    ConnectionState m_connectionState{ConnectionState::DISCONNECTED};
    std::shared_ptr<InitialConnection> m_temporaryOwnership;
};

#endif //INITIAL_CONNECTION_H
