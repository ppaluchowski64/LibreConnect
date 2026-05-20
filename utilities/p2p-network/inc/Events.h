#ifndef EVENTS_H
#define EVENTS_H

#include <system_error>
#include <DeviceInfo.h>
#include <InitialConnection.h>
#include <QEvent>

constexpr static int P2PEventBase = QEvent::User + 100;

enum class EventResult : bool {
    SUCCESS = 1,
    FAILURE = 0
};

class ConnectedEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(P2PEventBase);
    explicit ConnectedEvent(const EventResult result) : QEvent(Type), m_status(result) {}
    EventResult GetResult() const { return m_status; }

    ConnectedEvent* clone() const override {
        return new ConnectedEvent(*this);
    }

private:
    EventResult m_status;
};

class DisconnectedEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(P2PEventBase+1);
    explicit DisconnectedEvent(const std::error_code code) : QEvent(Type), m_errorCode(code) {}
    std::error_code GetErrorCode() const { return m_errorCode; }

    DisconnectedEvent* clone() const override {
        return new DisconnectedEvent(*this);
    }

private:
    std::error_code m_errorCode;
};

class ScannerErrorEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(P2PEventBase+2);
    explicit ScannerErrorEvent(const std::error_code errorCode) : QEvent(Type), m_errorCode(errorCode) {}
    std::error_code GetErrorCode() const { return m_errorCode; }

    ScannerErrorEvent* clone() const override {
        return new ScannerErrorEvent(*this);
    }

private:
    std::error_code m_errorCode;
};

class ConnectionPendingEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(P2PEventBase+3);
    explicit ConnectionPendingEvent(const DeviceInfo& deviceInfo, const InitialConnectionMode mode, std::string pairingCode, std::function<void(bool, std::string)>&& callback)
        : QEvent(Type), m_mode(mode), m_deviceInfo(deviceInfo), m_pairingCode(std::move(pairingCode)), m_callback(std::move(callback)) {}
    DeviceInfo GetDeviceInfo() const { return m_deviceInfo; }
    InitialConnectionMode GetInitialConnectionMode() const { return m_mode; }
    std::string GetPairingCode() const { return m_pairingCode; }

    void AcceptConnection() const { m_callback(true, ""); }
    void AcceptConnectionIfVerified(const std::string& challenge) const { m_callback(true, challenge); }
    void DenyConnection() const { m_callback(false, ""); }

    ConnectionPendingEvent* clone() const override {
        return new ConnectionPendingEvent(*this);
    }

private:
    InitialConnectionMode m_mode;
    DeviceInfo m_deviceInfo;
    std::string m_pairingCode;
    std::function<void(bool, std::string)> m_callback;

};

class ConnectionVerificationEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(P2PEventBase+4);
    explicit ConnectionVerificationEvent(std::function<void(std::string)>&& callback) : QEvent(Type), m_callback(std::move(callback)) {}

    void SendAnswer(const std::string& answer) const { m_callback(answer); }

    ConnectionVerificationEvent* clone() const override {
        return new ConnectionVerificationEvent(*this);
    }

private:
    std::function<void(std::string)> m_callback;

};

class ConnectionFailedVerificationEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(P2PEventBase+5);
    explicit ConnectionFailedVerificationEvent(const int32_t leftTries) : QEvent(Type), m_leftTries(leftTries) {}

    int32_t GetLeftTries() const { return m_leftTries; }

    ConnectionFailedVerificationEvent* clone() const override {
        return new ConnectionFailedVerificationEvent(*this);
    }

private:
    int32_t m_leftTries;

};

class ConnectionApprovalRequestedEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(P2PEventBase+9);
    explicit ConnectionApprovalRequestedEvent(const DeviceInfo& deviceInfo, std::function<void(bool)>&& callback)
        : QEvent(Type), m_deviceInfo(deviceInfo), m_callback(std::move(callback)) {}

    DeviceInfo GetDeviceInfo() const { return m_deviceInfo; }
    void AcceptConnection() const { m_callback(true); }
    void DenyConnection() const { m_callback(false); }

    ConnectionApprovalRequestedEvent* clone() const override {
        return new ConnectionApprovalRequestedEvent(*this);
    }

private:
    DeviceInfo m_deviceInfo;
    std::function<void(bool)> m_callback;
};

class ConnectionApprovalDeniedEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(P2PEventBase+10);

    explicit ConnectionApprovalDeniedEvent() : QEvent(Type) {}

    ConnectionApprovalDeniedEvent* clone() const override {
        return new ConnectionApprovalDeniedEvent(*this);
    }
};

class FindMyPhoneAlertStateEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(P2PEventBase + 6);
    explicit FindMyPhoneAlertStateEvent(const bool active) : QEvent(Type), m_active(active) {}

    bool IsActive() const { return m_active; }

    FindMyPhoneAlertStateEvent* clone() const override {
        return new FindMyPhoneAlertStateEvent(*this);
    }

private:
    bool m_active;
};

class DeviceNotPairedEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(P2PEventBase + 7);
    explicit DeviceNotPairedEvent(const std::string& deviceID) : QEvent(Type), m_deviceID(deviceID) {}

    std::string GetDeviceID() const { return m_deviceID; }

    DeviceNotPairedEvent* clone() const override {
        return new DeviceNotPairedEvent(*this);
    }

private:
    std::string m_deviceID;

};

class DeviceCooldownEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(P2PEventBase + 8);
    explicit DeviceCooldownEvent(const float duration) : QEvent(Type), m_leftDuration(duration) {}
    float LeftDuration() const { return m_leftDuration; }

    DeviceCooldownEvent* clone() const override {
        return new DeviceCooldownEvent(*this);
    }

private:
    float m_leftDuration;
};

#endif //EVENTS_H
