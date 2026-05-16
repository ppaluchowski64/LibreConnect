#ifndef SMSBRIDGEMODULE_H
#define SMSBRIDGEMODULE_H

#include <BaseModule.h>

#include <memory>

class SmsPermissionChangeGate;
class SmsPermissionEventListener;

class SmsBridgeModule : public BaseModule {
public:
    ~SmsBridgeModule() override;

protected:
    void EnableResponseCallbacks() override;
    void DisableResponseCallbacks() override;

    void OnInitialize() override;
    asio::awaitable<void> OnEnable() override;
    asio::awaitable<void> OnDisable() override;
    asio::awaitable<void> OnShutdown() override;

    const char* GetModuleName() const override;
    ModuleType GetModuleType() const override;

private:
    std::shared_ptr<SmsPermissionChangeGate> m_smsPermissionGate;
    std::shared_ptr<SmsPermissionEventListener> m_smsPermissionEventListener;
    bool m_smsPermissionRequestAnnounced = false;
};


#endif // SMSBRIDGEMODULE_H
