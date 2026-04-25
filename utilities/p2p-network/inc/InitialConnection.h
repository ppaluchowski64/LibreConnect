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
    DEVICE_DATA_FOR_CONNECTION,
    DEVICE_DATA_FOR_SEEKING_CONNECTION,
    CHALLENGE_ANSWER_REQUEST,
    CHALLENGE_RESPONSE,
    CHALLENGE_WRONG_ANSWER,
    DEVICE_IS_UNPAIRED,
    DEVICE_CONNECT_COOLDOWN
};

struct InitialConnectionData {
    InitialConnectionData() : deviceInfo(), initialConnectionMode(InitialConnectionMode::CONNECTION_WITHOUT_PAIR) {}

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
    explicit InitialConnection();
    static std::shared_ptr<InitialConnection> Create();

    void Connect(TCPEndpoint&& endpoint, InitialConnectionMode mode);
    void Seek(TCPEndpoint&& endpoint, std::function<void(TCPEndpoint endpoint)>&& callback);
    void Disconnect(bool cancelSeeking = false);

    void TemporaryOwnership(const std::shared_ptr<InitialConnection>& ptr);

private:
    template <Serializable... Args>
    void Send(InitialConnectionPackageType type, Args&&... args) {
        m_packagesOut.emplace_back(Package<InitialConnectionPackageType>::CreateUnique(type, std::forward<Args>(args)...));
        m_sendFlag.Signal();
    }

    asio::awaitable<void> CoConnect(TCPEndpoint endpoint, InitialConnectionMode mode);
    asio::awaitable<void> CoSeek(TCPEndpoint endpoint, std::function<void(TCPEndpoint endpoint)> callback);
    asio::awaitable<void> CoDisconnect(bool cancelSeeking);
    asio::awaitable<void> CoSend();
    asio::awaitable<void> CoReceive();

    asio::awaitable<void> CoProcessConnectionVerificationEvent(std::string response);
    asio::awaitable<void> CoProcessConnectionPendingCallback(bool actionResult, InitialConnectionData data, std::string challenge);
    asio::awaitable<void> CoPrimaryConnectionCallback(InitialConnectionData data);
    static std::string ComputePairingCode(const std::string& localFingerprint, const std::string& remoteFingerprint);

    IOContext& m_context;
    IOContextStrand m_strand;

    AwaitableFlag m_sendFlag;
    TCPSocket m_socket;
    std::string m_challengeResult;
    std::string m_localCertificateFingerprint;
    std::string m_expectedChallengeCode;
    InitialConnectionMode m_requestedConnectionMode{InitialConnectionMode::CONNECTION_WITHOUT_PAIR};
    bool m_finalHandshakeCompleted{false};

    std::deque<std::unique_ptr<Package<InitialConnectionPackageType>>> m_packagesOut;
    std::shared_ptr<InitialConnection> m_temporaryOwnership;

    ConnectionState m_connectionState{ConnectionState::DISCONNECTED};
    int32_t m_challengeLeftTries;
    bool m_sendInFlight{false};
    bool m_receivedTerminalHandshakeReason{false};
};

#endif //INITIAL_CONNECTION_H
