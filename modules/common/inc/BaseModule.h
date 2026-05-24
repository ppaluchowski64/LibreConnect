#ifndef BASE_MODULE_H
#define BASE_MODULE_H

#include <atomic>
#include <cstdint>

#include <DebugLog.h>
#include <asio.hpp>

#include <AsioCommon.h>
#include <ThreadPool.h>
#include <ConnectionManager.h>

#include <QEvent>
#include <magic_enum/magic_enum.hpp>

typedef asio::io_context IOContext;

enum class ModuleState : uint8_t {
    Uninitialized,
    Initializing,
    Enabling,
    Enabled,
    Disabling,
    Disabled
};

enum class ModuleFailReason : uint8_t
{
    None = 0,                 // No failure
    IncorrectConfig,          // Invalid or missing configuration
    NotInitialized,           // Module used before proper initialization
    InitializationFailed,     // Initialization attempted but failed
    UnsupportedPlatform,      // Platform or OS version is not supported
    Timeout,                  // Operation timed out
    InvalidState,             // Operation not allowed in current state
    InternalError,            // Unexpected internal failure
    Unknown                   // Fallback for unmapped or future errors
};

enum class ModuleType : uint8_t {
    Unknown = 0,              // Invalid state
    NotificationSync,
    NetworkCamera,
    NetworkFileSystem,
    ClipboardSync,
    RemoteInput,
    SmsBridge,
    NetworkMicrophone
};

enum class PermissionType : uint16_t {
    Unknown = 0,
    Camera,
    Notifications,
    FileSystem,
    Battery,
    Accessibility,
    Sms,
    DesktopNotifications,
    Microphone
};

inline const char* ModuleFailReasonToString(const ModuleFailReason reason)
{
    switch (reason) {
    case ModuleFailReason::None:
        return "No failure";
    case ModuleFailReason::IncorrectConfig:
        return "Incorrect configuration";
    case ModuleFailReason::NotInitialized:
        return "Not initialized";
    case ModuleFailReason::InitializationFailed:
        return "Initialization failed";
    case ModuleFailReason::UnsupportedPlatform:
        return "Unsupported platform";
    case ModuleFailReason::Timeout:
        return "Timeout";
    case ModuleFailReason::InvalidState:
        return "Invalid state";
    case ModuleFailReason::InternalError:
        return "Internal error";
    case ModuleFailReason::Unknown:
    default:
        return "Unknown";
    }
}

inline const char* ModuleTypeToString(const ModuleType type)
{
    switch (type) {
    case ModuleType::NotificationSync:
        return "NotificationSync";
    case ModuleType::NetworkCamera:
        return "NetworkCamera";
    case ModuleType::NetworkFileSystem:
        return "NetworkFileSystem";
    case ModuleType::ClipboardSync:
        return "ClipboardSync";
    case ModuleType::RemoteInput:
        return "RemoteInput";
    case ModuleType::SmsBridge:
        return "SmsBridge";
    case ModuleType::NetworkMicrophone:
        return "NetworkMicrophone";
    case ModuleType::Unknown:
    default:
        return "Unknown";
    }
}

class ModuleErrorEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(QEvent::User + 300);
    explicit ModuleErrorEvent(const ModuleFailReason failReason, const ModuleType type) : QEvent(Type), m_error(failReason), m_moduleType(type) {}
    ModuleFailReason GetError() const { return m_error; }
    ModuleType GetModuleType() const { return m_moduleType; }

    ModuleErrorEvent* clone() const override {
        return new ModuleErrorEvent(*this);
    }

private:
    ModuleFailReason m_error;
    ModuleType m_moduleType;
};

class ModuleRequestedPermission final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(QEvent::User + 301);
    explicit ModuleRequestedPermission(const PermissionType type) : QEvent(Type), m_permissionType(type) {}
    PermissionType GetPermissionType() const { return m_permissionType; }

    ModuleRequestedPermission* clone() const override {
        return new ModuleRequestedPermission(*this);
    }

private:
    PermissionType m_permissionType;

};

class ModuleRequestedPermissionRejected final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(QEvent::User + 302);
    explicit ModuleRequestedPermissionRejected (const PermissionType type) : QEvent(Type), m_permissionType(type) {}
    PermissionType GetPermissionType() const { return m_permissionType; }

    ModuleRequestedPermissionRejected * clone() const override {
        return new ModuleRequestedPermissionRejected(*this);
    }

private:
    PermissionType m_permissionType;
};


class ModuleRequestedPermissionGranted final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(QEvent::User + 303);
    explicit ModuleRequestedPermissionGranted (const PermissionType type) : QEvent(Type), m_permissionType(type) {}
    PermissionType GetPermissionType() const { return m_permissionType; }

    ModuleRequestedPermissionGranted * clone() const override {
        return new ModuleRequestedPermissionGranted(*this);
    }

private:
    PermissionType m_permissionType;
};


constexpr const char* APPLICATION_NAME = "LibreConnect";

class BaseModule : public std::enable_shared_from_this<BaseModule> {
public:
    explicit BaseModule() : m_context(ThreadPool::GetContext()), m_moduleStrand(asio::make_strand(m_context)) {}
    virtual ~BaseModule() = default;

    void Initialize(const bool disableWarnings = false) {
        if (GetModuleState() != ModuleState::Uninitialized) {
            if (!disableWarnings) {
                Debug::LogWarning("{}: Initialize skipped - module already initialized", GetModuleName());
            }

            return;
        }

        const uint64_t generation = m_lifecycleGeneration.fetch_add(1) + 1;
        Debug::Log("{}: Initialize started", GetModuleName());
        SetModuleState(ModuleState::Initializing);
        EnableResponseCallbacks();
        OnInitialize();
        if (m_lifecycleGeneration.load() != generation) {
            Debug::LogWarning("{}: Initialize completion ignored - lifecycle changed", GetModuleName());
            return;
        }
        SetModuleState(ModuleState::Disabled);
        Debug::Log("{}: Initialize completed", GetModuleName());
    }

    void Enable(const bool disableWarnings = false) {
        asio::co_spawn(m_moduleStrand, EnableAwaitable(disableWarnings), asio::detached);
    }

    void Disable(const bool disableWarnings = false) {
        asio::co_spawn(m_moduleStrand, DisableAwaitable(disableWarnings), asio::detached);
    }

    void Shutdown(const bool disableWarnings = false) {
        const ModuleState state = GetModuleState();

        if (state == ModuleState::Uninitialized) {
            if (!disableWarnings) {
                Debug::LogWarning("{}: Shutdown skipped - module is not initialized", GetModuleName());
            }

            return;
        }

        const uint64_t generation = m_lifecycleGeneration.fetch_add(1) + 1;
        Debug::Log("{}: Shutdown started", GetModuleName());
        DisableResponseCallbacks();
        SetModuleState(ModuleState::Uninitialized);

        asio::co_spawn(m_moduleStrand, ShutdownAwaitable(disableWarnings, state, generation), asio::detached);
    }

    asio::awaitable<void> EnableAwaitable(const bool disableWarnings = false) {
        const std::shared_ptr<BaseModule> instance = shared_from_this();
        ModuleState state = GetModuleState();

        if (state == ModuleState::Uninitialized) {
            Debug::Log("{}: Initialize started (via Enable)", GetModuleName());
            SetModuleState(ModuleState::Initializing);
            EnableResponseCallbacks();
            OnInitialize();
            SetModuleState(ModuleState::Disabled);
            Debug::Log("{}: Initialize completed (via Enable)", GetModuleName());
            state = ModuleState::Disabled;
        }

        if (state != ModuleState::Disabled) {
            if (disableWarnings) {
                co_return;
            }

            if (state == ModuleState::Enabled) {
                Debug::LogWarning("{}: Enable skipped - module already enabled", GetModuleName());
                co_return;
            }

            Debug::LogWarning("{}: Enable skipped - invalid state {}", GetModuleName(), static_cast<int>(state));
            co_return;
        }

        Debug::Log("{}: Enable started", GetModuleName());
        SetModuleState(ModuleState::Enabling);
        try {
            co_await OnEnable();
            const ModuleState currentState = GetModuleState();
            if (currentState != ModuleState::Enabling) {
                Debug::LogWarning(
                    "{}: Enable completion ignored - state changed to {}",
                    GetModuleName(),
                    magic_enum::enum_name(currentState)
                );
                co_return;
            }
            SetModuleState(ModuleState::Enabled);
            Debug::Log("{}: Enable completed", GetModuleName());
        } catch (const std::exception& exc) {
            if (GetModuleState() == ModuleState::Enabling) {
                SetModuleState(ModuleState::Disabled);
            }
            Debug::LogError("{}: Enable failed with exception: {}", GetModuleName(), exc.what());
            ProcessError(ModuleFailReason::InternalError);
        }
    }

    asio::awaitable<void> DisableAwaitable(const bool disableWarnings = false) {
        const std::shared_ptr<BaseModule> instance = shared_from_this();
        const ModuleState state = GetModuleState();

        if (state != ModuleState::Enabled && state != ModuleState::Enabling) {
            if (disableWarnings) {
                co_return;
            }

            Debug::LogWarning("{}: Disable skipped - module is not enabled", GetModuleName());
            co_return;
        }

        if (state == ModuleState::Enabling) {
            Debug::Log("{}: Disable started - cancelling enable in progress", GetModuleName());
        } else {
            Debug::Log("{}: Disable started", GetModuleName());
        }

        SetModuleState(ModuleState::Disabling);
        try {
            co_await OnDisable();
            if (GetModuleState() != ModuleState::Disabling) {
                Debug::LogWarning(
                    "{}: Disable completion ignored - state changed to {}",
                    GetModuleName(),
                    magic_enum::enum_name(GetModuleState())
                );
                co_return;
            }
            SetModuleState(ModuleState::Disabled);
            Debug::Log("{}: Disable completed", GetModuleName());
        } catch (const std::exception& exc) {
            if (GetModuleState() == ModuleState::Disabling) {
                SetModuleState(ModuleState::Disabled);
            }
            Debug::LogError("{}: Disable failed with exception: {}", GetModuleName(), exc.what());
            ProcessError(ModuleFailReason::InternalError);
        }
    }

    asio::awaitable<void> ShutdownAwaitable(
        const bool disableWarnings = false,
        const ModuleState shutdownState = ModuleState::Uninitialized,
        const uint64_t shutdownGeneration = 0
    ) {
        (void)disableWarnings;
        const std::shared_ptr<BaseModule> instance = shared_from_this();

        if (shutdownState == ModuleState::Enabled || shutdownState == ModuleState::Enabling) {
            try {
                co_await OnDisable();
            } catch (const std::exception& exc) {
                Debug::LogError("{}: Disable during shutdown failed with exception: {}", GetModuleName(), exc.what());
            }
        }

        try {
            co_await OnShutdown();
        } catch (const std::exception& exc) {
            Debug::LogError("{}: Shutdown failed with exception: {}", GetModuleName(), exc.what());
        }

        if (m_lifecycleGeneration.load() != shutdownGeneration) {
            Debug::LogWarning("{}: Shutdown completion ignored - lifecycle changed", GetModuleName());
            co_return;
        }

        Debug::Log("{}: Shutdown completed", GetModuleName());
    }

    ModuleState GetModuleState() const {
        return m_state.load();
    }

    ModuleFailReason GetModuleFailReason() const {
        return m_failReason.load();
    }

private:
    void SetModuleState(const ModuleState state) {
        m_state.store(state);
    }

    void SetModuleFailReason(const ModuleFailReason reason) const {
        m_failReason.store(reason);
    }

protected:
    IOContext& m_context;
    IOContextStrand m_moduleStrand;

    std::atomic<ModuleState> m_state = ModuleState::Uninitialized;
    std::atomic<bool> m_peerModuleEnabled = false;
    std::atomic<uint64_t> m_lifecycleGeneration = 0;
    mutable std::atomic<ModuleFailReason> m_failReason = ModuleFailReason::None;

    bool ShouldAbortEnable() const {
        const ModuleState state = GetModuleState();
        return state != ModuleState::Enabling && state != ModuleState::Enabled;
    }

    virtual void EnableResponseCallbacks() = 0;
    virtual void DisableResponseCallbacks() = 0;

    virtual void OnInitialize() = 0;
    virtual asio::awaitable<void> OnEnable() = 0;
    virtual asio::awaitable<void> OnDisable() = 0;
    virtual asio::awaitable<void> OnShutdown() = 0;

    virtual const char* GetModuleName() const {
        return "Invalid Module";
    }

    virtual ModuleType GetModuleType() const {
        return ModuleType::Unknown;
    }

    void ProcessError(const ModuleFailReason reason) const {
        m_failReason.store(reason);
        const std::unique_ptr<QEvent> event = std::make_unique<ModuleErrorEvent>(reason, GetModuleType());
        ConnectionManager::SendEvent(event);
    }
};

#endif //BASE_MODULE_H
