#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H

#include <atomic>
#include <PrimaryConnection.h>
#include <ConcurrentUnorderedMap.h>
#include <magic_enum/magic_enum.hpp>

class ConnectionManager final {
public:
    typedef std::function<void(std::unique_ptr<Package<PC_PackageType>>&&)> RequestCallbackType;
    typedef std::function<void(bool)> ConnectionCallbackType;

    static void Connect(const TCPEndpoint& endpoint, const ConnectionCallbackType& callback);
    static void AddResponseHandler(PC_PackageType type, RequestCallbackType&& handler);

    template <StdLayoutOrVecOrString... Args>
    static void SendRequest(PC_PackageType type, Args&&... args) {
        std::call_once(s_initFlag, Initialize);

        if (!s_instance->m_isConnected.load()) {
            Debug::LogWarning("Failed to send request of type \'{}\': primary connection is inactive.", magic_enum::enum_name(type));
            return;
        }

        s_instance->m_primaryConnection->Send(type, std::forward<Args>(args)...);
    }

    template <StdLayoutOrVecOrString... Args>
    static void SendRequestWithResponse(PC_PackageType type, Args&&... args, RequestCallbackType requestResponseCallback) {
        std::call_once(s_initFlag, Initialize);

        if (!s_instance->m_isConnected.load()) {
            Debug::LogWarning("Failed to send request of type \'{}\': primary connection is inactive.", magic_enum::enum_name(type));
            return;
        }

        constexpr uint8_t packageFlag = static_cast<uint8_t>(PackageFlag::REQUEST_WITH_RESPONSE);
        const size_t requestID = s_instance->m_currentRequestID.fetch_add(1);
        s_instance->m_primaryConnection->SendWithFlag(type, packageFlag, requestID, std::forward<Args>(args)...);
        s_instance->m_requestCallbackMap.InsertOrAssign(requestID, std::forward<RequestCallbackType>(requestResponseCallback));
    }

private:
    static void Initialize();

    static ConnectionManager* s_instance;
    static std::once_flag s_initFlag;

    std::atomic<size_t> m_currentRequestID{0};
    std::atomic<bool>   m_shutdownRequested{false};
    std::atomic<bool>   m_isConnected{false};

    std::shared_ptr<PrimaryConnection> m_primaryConnection;
    ConcurrentUnorderedMap<size_t, RequestCallbackType> m_requestCallbackMap;
    ConcurrentUnorderedMap<PC_PackageType, RequestCallbackType> m_responseHandlerMap;
};

#endif //CONNECTION_MANAGER_H
