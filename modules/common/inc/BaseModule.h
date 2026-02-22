#ifndef BASE_MODULE_H
#define BASE_MODULE_H

#include <atomic>

#include <DebugLog.h>
#include <asio.hpp>

#include <AsioCommon.h>
#include <ThreadPool.h>

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

constexpr const char* APPLICATION_NAME = "LibreConnect";

class BaseModule : public std::enable_shared_from_this<BaseModule> {
public:
    explicit BaseModule() : m_context(ThreadPool::GetContext()), m_moduleStrand(asio::make_strand(m_context)) {}
    virtual ~BaseModule() = default;

    void Initialize() {
        if (GetModuleState() != ModuleState::Uninitialized) {
            Debug::LogWarning("[Module::Initialize] Module already initialized");
            return;
        }

        SetModuleState(ModuleState::Initializing);
        EnableResponseCallbacks();
        OnInitialize();
        SetModuleState(ModuleState::Disabled);
    }

    void Enable(const bool disableWarnings = false) {
        asio::co_spawn(m_moduleStrand, EnableAwaitable(disableWarnings), asio::detached);
    }

    void Disable(const bool disableWarnings = false) {
        asio::co_spawn(m_moduleStrand, DisableAwaitable(disableWarnings), asio::detached);
    }

    void Shutdown(const bool disableWarnings = false) {
        asio::co_spawn(m_moduleStrand, ShutdownAwaitable(disableWarnings), asio::detached);
    }

    asio::awaitable<void> EnableAwaitable(const bool disableWarnings = false) {
        const std::shared_ptr<BaseModule> instance = shared_from_this();
        const ModuleState state = GetModuleState();

        if (state != ModuleState::Disabled) {
            if (disableWarnings) {
                co_return;
            }

            if (state == ModuleState::Enabled) {
                Debug::LogWarning("[Module::Enable] Module already enabled");
                co_return;
            }

            Debug::LogWarning("[Module::Enable] Module enable failed");
            co_return;
        }

        SetModuleState(ModuleState::Enabling);
        try {
            co_await OnEnable();
            SetModuleState(ModuleState::Enabled);
        } catch (const std::exception& exc) {
            SetModuleState(ModuleState::Disabled);
            Debug::LogError("[Module::EnableHelper] Exception during OnEnable: {}", exc.what());
            throw;
        }
    }

    asio::awaitable<void> DisableAwaitable(const bool disableWarnings = false) {
        const std::shared_ptr<BaseModule> instance = shared_from_this();
        const ModuleState state = GetModuleState();

        if (state != ModuleState::Enabled) {
            if (disableWarnings) {
                co_return;
            }

            Debug::LogWarning("[Module::Disable] Module isn't enabled");
            co_return;
        }

        SetModuleState(ModuleState::Disabling);
        try {
            co_await OnDisable();
            SetModuleState(ModuleState::Disabled);
        } catch (const std::exception& exc) {
            SetModuleState(ModuleState::Disabled);
            Debug::LogError("[Module::DisableHelper] Exception during OnDisable: {}", exc.what());
            throw;
        }
    }

    asio::awaitable<void> ShutdownAwaitable(const bool disableWarnings = false) {
        const std::shared_ptr<BaseModule> instance = shared_from_this();
        const ModuleState state = GetModuleState();

        if (state == ModuleState::Uninitialized) {
            if (disableWarnings) {
                co_return;
            }

            Debug::LogWarning("[Module::Shutdown] Module is not initialized");
            co_return;
        }

        DisableResponseCallbacks();

        if (GetModuleState() == ModuleState::Enabled) {
            co_await DisableAwaitable();
        }

        try {
            co_await OnShutdown();
        } catch (const std::exception& exc) {
            Debug::LogError("[Module::Shutdown] Exception during Shutdown: {}", exc.what());
        }

        SetModuleState(ModuleState::Uninitialized);
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

    void SetModuleFailReason(const ModuleFailReason reason) {
        m_failReason.store(reason);
    }

protected:
    IOContext& m_context;
    IOContextStrand m_moduleStrand;

    std::atomic<ModuleState> m_state = ModuleState::Uninitialized;
    std::atomic<ModuleFailReason> m_failReason = ModuleFailReason::None;

    virtual void EnableResponseCallbacks() = 0;
    virtual void DisableResponseCallbacks() = 0;

    virtual void OnInitialize() = 0;
    virtual asio::awaitable<void> OnEnable() = 0;
    virtual asio::awaitable<void> OnDisable() = 0;
    virtual asio::awaitable<void> OnShutdown() = 0;
};

#endif //BASE_MODULE_H
