#ifndef EVENTS_H
#define EVENTS_H

#include <system_error>
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
    EventResult Result() const { return m_status; }

private:
    EventResult m_status;
};



class DisconnectedEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(P2PEventBase+1);
    explicit DisconnectedEvent(const std::error_code code) : QEvent(Type), m_errorCode(code) {}
    std::error_code ErrorCode() const { return m_errorCode; }
private:
    std::error_code m_errorCode;
};


#endif //EVENTS_H
