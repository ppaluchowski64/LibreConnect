#ifndef SMSBRIDGEEVENTS_H
#define SMSBRIDGEEVENTS_H

#include <QEvent>
#include <string>
#include <vector>

constexpr static int SmsBridgeEventBase = QEvent::User + 500;

class FetchContactListResultEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(SmsBridgeEventBase);
    explicit FetchContactListResultEvent(const std::vector<std::pair<std::string, std::string>>& contacts) : QEvent(Type), m_contacts(contacts) {}

    // First element is contact name, second element is phone number
    const std::vector<std::pair<std::string, std::string>>& GetContacts() const { return m_contacts; }

    FetchContactListResultEvent* clone() const override {
        return new FetchContactListResultEvent(*this);
    }

private:
    const std::vector<std::pair<std::string, std::string>> m_contacts;

};

class FetchMessageListResultEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(SmsBridgeEventBase+1);
    explicit FetchMessageListResultEvent(const std::vector<std::string>& messages, const std::string& number) : QEvent(Type), m_messages(messages), m_number(number) {}
    const std::vector<std::string>& GetMessages() const { return m_messages; }
    const std::string& GetNumber() const { return m_number; }

    FetchMessageListResultEvent* clone() const override {
        return new FetchMessageListResultEvent(*this);
    }

private:
    const std::string m_number;
    const std::vector<std::string> m_messages;

};

class SendSmsResultEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(SmsBridgeEventBase+2);
    explicit SendSmsResultEvent(const bool result, const uuid messageUUID) : QEvent(Type), m_success(result), m_messageUUID(messageUUID) {}
    bool Success() const { return m_success; }
    uuid GetMessageUUID() const { return m_messageUUID; }

    SendSmsResultEvent* clone() const override {
        return new SendSmsResultEvent(*this);
    }

private:
    bool m_success;
    uuid m_messageUUID;

};

#endif // SMSBRIDGEEVENTS_H
