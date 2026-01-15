#ifndef BASE_MODULE_H
#define BASE_MODULE_H

#include <cstdint>
#include <atomic>
#include <thread>
#include <vector>

#include <DebugLog.h>
#include <asio.hpp>

typedef asio::io_context IOContext;

enum class ModuleState : uint8_t {
    Uninitialized,
    Initializing,
    Enabling,
    Enabled,
    Disabling,
    Disabled
};

class BaseModule {
public:
    virtual ~BaseModule() = default;

    void Initialize() {
        if (GetModuleState() != ModuleState::Uninitialized) {
            Debug::LogWarning("[Module::Initialize] Module already initialized");
            return;
        }

        SetModuleState(ModuleState::Initializing);
        m_threadCount.store(0);
        OnInitialize();
        SetModuleState(ModuleState::Disabled);
    }

    void Enable() {
        const ModuleState state = GetModuleState();

        if (state != ModuleState::Disabled) {
            if (state == ModuleState::Enabled) {
                Debug::LogWarning("[Module::Enable] Module already enabled");
                return;
            }

            Debug::LogWarning("[Module::Enable] Module enable failed");
            return;
        }

        asio::co_spawn(m_context, EnableHelper(), asio::detached);
    }

    void Disable() {
        const ModuleState state = GetModuleState();
        if (state != ModuleState::Enabled) {
            Debug::LogWarning("[Module::Disable] Module isn't enabled");
            return;
        }

        asio::co_spawn(m_context, DisableHelper(), asio::detached);
    }

    void Shutdown() {
        const ModuleState state = GetModuleState();
        if (state == ModuleState::Uninitialized) {
            Debug::LogWarning("[Module::Shutdown] Module is not initialized");
            return;
        }

        asio::co_spawn(m_context, ShutdownHelper(), asio::detached);
    }

    ModuleState GetModuleState() const {
        return m_state.load();
    }

private:
    void SetModuleState(const ModuleState state) {
        m_state.store(state);
    }

    void JoinThreads() {
        m_context.stop();

        for (auto& thread : m_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    asio::awaitable<void> EnableHelper() {
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

    asio::awaitable<void> DisableHelper() {
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

    asio::awaitable<void> ShutdownHelper() {
        if (GetModuleState() == ModuleState::Enabled) {
            co_await DisableHelper();
        }

        try {
            co_await OnShutdown();
        } catch (const std::exception& exc) {
            Debug::LogError("[Module::Shutdown] Exception during Shutdown: {}", exc.what());
        }

        SetModuleState(ModuleState::Uninitialized);
        JoinThreads();
    }

protected:
    IOContext m_context;
    std::vector<std::thread> m_threads;
    std::atomic<uint8_t> m_threadCount;
    std::atomic<ModuleState> m_state = ModuleState::Uninitialized;

    virtual void OnInitialize() = 0;
    virtual asio::awaitable<void> OnEnable() = 0;
    virtual asio::awaitable<void> OnDisable() = 0;
    virtual asio::awaitable<void> OnShutdown() = 0;

    void AddThreads(const uint8_t count) {
        const ModuleState state = GetModuleState();

        if (state != ModuleState::Initializing) {
            Debug::LogWarning("[Module::AddThreads] Thread count can be modified only during initialization of module");
            return;
        }

        uint8_t threadCount = m_threadCount.load();
        if (UINT8_MAX - threadCount < count) {
            Debug::LogWarning("[Module::AddThreads] Thread amount limit reached");
            return;
        }

        threadCount += count;
        m_threadCount.store(threadCount);

        for (uint8_t i = 0; i < count; i++) {
            m_threads.emplace_back(std::thread([this] {m_context.run();}));
        }
    }
};

#endif //BASE_MODULE_H
