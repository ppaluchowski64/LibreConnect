#ifndef SYSTEMINFOSHAREMODULE_H
#define SYSTEMINFOSHAREMODULE_H

#include <BaseModule.h>
#include <asio/awaitable.hpp>

#include <atomic>

#include <QEvent>

class PeerBatteryLevelUpdateEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(QEvent::User + 304);
    explicit PeerBatteryLevelUpdateEvent(const float level) : QEvent(Type), m_batteryLevel(level) {}
    float GetBatteryLevel() const { return m_batteryLevel; }

    PeerBatteryLevelUpdateEvent* clone() const override {
        return new PeerBatteryLevelUpdateEvent(*this);
    }

private:
    float m_batteryLevel;
};


class SystemInfoShareModule : public BaseModule {
private:
    asio::awaitable<void> SendBatteryInfo(uint64_t senderGeneration) const;
    mutable std::atomic<uint64_t> m_batterySenderGeneration{0};

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

#endif // SYSTEMINFOSHAREMODULE_H
