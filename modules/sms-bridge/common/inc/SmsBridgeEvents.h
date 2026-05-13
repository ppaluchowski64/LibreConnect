#ifndef SMSBRIDGEEVENTS_H
#define SMSBRIDGEEVENTS_H

#include <QEvent>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <utility>

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
    boost::uuids::uuid m_messageUUID;

};

class NewSmsReceivedEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(SmsBridgeEventBase+3);
    explicit NewSmsReceivedEvent(std::string sender, std::string body, const int64_t timestamp)
        : QEvent(Type)
        , m_sender(std::move(sender))
        , m_body(std::move(body))
        , m_timestamp(timestamp) {}

    const std::string& GetSender() const { return m_sender; }
    const std::string& GetBody() const { return m_body; }
    int64_t GetTimestamp() const { return m_timestamp; }

    NewSmsReceivedEvent* clone() const override {
        return new NewSmsReceivedEvent(*this);
    }

private:
    std::string m_sender;
    std::string m_body;
    int64_t m_timestamp = 0;
};

class MMSContentReceivedEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(SmsBridgeEventBase+4);
    explicit MMSContentReceivedEvent(const std::string& target, const std::filesystem::path& destination) : QEvent(Type), m_target(target), m_destination(destination) {}

    std::string GetTarget() const { return m_target; }
    std::filesystem::path GetDestination() const { return m_destination; }

    MMSContentReceivedEvent* clone() const override {
        return new MMSContentReceivedEvent(*this);
    }

private:
    std::string m_target;
    std::filesystem::path m_destination;
};

#endif // SMSBRIDGEEVENTS_H
