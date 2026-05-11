#ifndef SMSBRIDGEMODULE_H
#define SMSBRIDGEMODULE_H

#include <BaseModule.h>

#include <optional>
#include <filesystem>

class SmsBridgeModule : public BaseModule {
public:
    uuid SendSMS(const std::string& target, const std::string& message) const;
    void GetContactList() const;
    void GetTargetMessages(const std::string& target) const;
    void FetchMMSContent(const std::string& target) const;
    std::optional<std::filesystem::path> GetMMSContentPath(const std::string& target) const;

private:
    asio::awaitable<void> SendSMSAwaitable(std::string target, std::string message, uuid messageUUID) const;
    asio::awaitable<void> GetContactListAwaitable() const;
    asio::awaitable<void> GetTargetMessagesAwaitable(std::string target) const;
    asio::awaitable<void> FetchMMSContentAwaitable(std::string target) const;

protected:
    void EnableResponseCallbacks() override;
    void DisableResponseCallbacks() override;

    void OnInitialize() override;
    asio::awaitable<void> OnEnable() override;
    asio::awaitable<void> OnDisable() override;
    asio::awaitable<void> OnShutdown() override;

    const char* GetModuleName() const override;
    ModuleType GetModuleType() const override;
};


#endif // SMSBRIDGEMODULE_H
