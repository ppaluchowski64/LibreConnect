#include <SystemInfoShareModule.h>
#include <SystemInfo.h>

asio::awaitable<void> SystemInfoShareModule::SendBatteryInfo(const uint64_t senderGeneration) const {
    float currentBattery = -1;

    asio::steady_timer timer(m_context.get_executor());
    while (m_batterySenderGeneration.load() == senderGeneration && !ShouldAbortEnable()) {
        const float result = SystemInfo::GetBatteryLevel();
        if (result != currentBattery && ConnectionManager::GetConnectionState() == ConnectionState::CONNECTED) {
            currentBattery = result;
            ConnectionManager::Send(PC_PackageType::SYSTEM_INFO_SHARE_MODULE_NEW_BATTERY_LEVEL, currentBattery);
        }

        timer.expires_after(std::chrono::milliseconds(100));
        co_await timer.async_wait();
    }
}

void SystemInfoShareModule::EnableResponseCallbacks() {
    const std::shared_ptr<SystemInfoShareModule> instance = std::static_pointer_cast<SystemInfoShareModule>(shared_from_this());

    ConnectionManager::AddResponseHandler(PC_PackageType::SYSTEM_INFO_SHARE_MODULE_ENABLE, [instance](PC_Package&& package) mutable {
        Debug::Log("SystemInfoShareModule: Received enable request");
        if (instance->GetModuleState() == ModuleState::Enabled) {
            Debug::Log("SystemInfoShareModule: Already enabled, sending state confirmation");
            ConnectionManager::Send(PC_PackageType::SYSTEM_INFO_SHARE_MODULE_STATE_CHANGED, true);
            return;
        }
        instance->Enable(true);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::SYSTEM_INFO_SHARE_MODULE_DISABLE, [instance](PC_Package&& package) mutable {
        Debug::Log("SystemInfoShareModule: Received disable request");
        if (instance->GetModuleState() == ModuleState::Disabled) {
            Debug::Log("SystemInfoShareModule: Already disabled, sending state confirmation");
            ConnectionManager::Send(PC_PackageType::SYSTEM_INFO_SHARE_MODULE_STATE_CHANGED, false);
            return;
        }
        instance->Disable(true);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::SYSTEM_INFO_SHARE_MODULE_STATE_CHANGED, [instance](PC_Package&& package) mutable {
        const bool peerEnabled = package->GetValue<bool>();
        Debug::Log("SystemInfoShareModule: Peer module state changed: {}", peerEnabled);
        instance->m_peerModuleEnabled.store(peerEnabled);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::SYSTEM_INFO_SHARE_MODULE_NEW_BATTERY_LEVEL, [instance](PC_Package&& package) mutable {
        const float batteryLevel = package->GetValue<float>();
        const std::unique_ptr<QEvent> event  = std::make_unique<PeerBatteryLevelUpdateEvent>(batteryLevel);
        ConnectionManager::SendEvent(event);
    });
}

void SystemInfoShareModule::DisableResponseCallbacks() {
    ConnectionManager::RemoveResponseHandler(PC_PackageType::SYSTEM_INFO_SHARE_MODULE_ENABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::SYSTEM_INFO_SHARE_MODULE_DISABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::SYSTEM_INFO_SHARE_MODULE_STATE_CHANGED);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::SYSTEM_INFO_SHARE_MODULE_NEW_BATTERY_LEVEL);
}

void SystemInfoShareModule::OnInitialize() {}

asio::awaitable<void> SystemInfoShareModule::OnEnable() {
    const uint64_t senderGeneration = m_batterySenderGeneration.fetch_add(1) + 1;
    m_peerModuleEnabled.store(false);
    ConnectionManager::Send(PC_PackageType::SYSTEM_INFO_SHARE_MODULE_ENABLE);
    ConnectionManager::Send(PC_PackageType::SYSTEM_INFO_SHARE_MODULE_STATE_CHANGED, true);
    asio::co_spawn(m_context, SendBatteryInfo(senderGeneration), asio::detached);
    co_return;
}

asio::awaitable<void> SystemInfoShareModule::OnDisable() {
    m_batterySenderGeneration.fetch_add(1);
    m_peerModuleEnabled.store(false);
    ConnectionManager::Send(PC_PackageType::SYSTEM_INFO_SHARE_MODULE_DISABLE);
    ConnectionManager::Send(PC_PackageType::SYSTEM_INFO_SHARE_MODULE_STATE_CHANGED, false);
    co_return;
}

asio::awaitable<void> SystemInfoShareModule::OnShutdown() {
    co_return;
}

const char* SystemInfoShareModule::GetModuleName() const {
    return "SystemInfoShareModule";
}

ModuleType SystemInfoShareModule::GetModuleType() const {
    return ModuleType::ClipboardSync;
}
