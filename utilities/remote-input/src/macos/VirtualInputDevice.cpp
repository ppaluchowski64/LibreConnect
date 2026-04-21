#include "VirtualInputDevice.h"

#include <DebugLog.h>
#include <ThreadPool.h>

#include <ApplicationServices/ApplicationServices.h>

#include <asio.hpp>
#include <concurrentqueue.h>

#include <chrono>
#include <system_error>

VirtualInputDevice::VirtualInputDevice(const char* /*deviceName*/) :
    m_eventFlag(ThreadPool::GetContext().get_executor()),
    m_isRunning(true) {

    m_source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);

    if (m_source)
        CGEventSourceSetLocalEventsSuppressionInterval(m_source, 0.0);

    asio::co_spawn(ThreadPool::GetContext(), CoProcessEvents(), asio::detached);
}

VirtualInputDevice::~VirtualInputDevice() {
    m_isRunning.store(false);
    m_eventFlag.Signal();

    if (m_source) {
        CFRelease(m_source);
        m_source = nullptr;
    }
}

void VirtualInputDevice::EmitNativeKeyPress(int nativeKeyCode) {
    static thread_local moodycamel::ProducerToken producerToken(m_eventQueue);
    m_eventQueue.enqueue(producerToken, {nativeKeyCode, true});
    m_eventFlag.Signal();
}

void VirtualInputDevice::EmitNativeKeyRelease(int nativeKeyCode) {
    static thread_local moodycamel::ProducerToken producerToken(m_eventQueue);
    m_eventQueue.enqueue(producerToken, {nativeKeyCode, false});
    m_eventFlag.Signal();
}

asio::awaitable<void> VirtualInputDevice::CoProcessEvents() {
    try {
        moodycamel::ConsumerToken consumerToken(m_eventQueue);
        asio::steady_timer timer(ThreadPool::GetContext().get_executor());

        while (m_isRunning.load()) {
            co_await m_eventFlag.Wait();

            KeyEvent ev{};
            while (m_isRunning.load() && m_eventQueue.try_dequeue(consumerToken, ev)) {
                if (!m_source) continue;

                CGEventRef event = CGEventCreateKeyboardEvent(
                    m_source,
                    static_cast<CGKeyCode>(ev.keyCode),
                    ev.isPress
                );

                if (event) {
                    CGEventPost(kCGHIDEventTap, event);
                    CFRelease(event);
                }

                timer.expires_after(std::chrono::milliseconds(5));
                co_await timer.async_wait(asio::use_awaitable);
            }
        }
    } catch (const std::system_error& error) {
        if (error.code() != asio::error::operation_aborted) {
            Debug::LogError("VirtualInputDevice::CoProcessEvents Asio error: {} (code: {})", error.what(), error.code().value());
        }
    } catch (const std::exception& e) {
        Debug::LogError("VirtualInputDevice::CoProcessEvents exception: {}", e.what());
    }
}
