#ifndef AWAITABLE_FLAG_H_
#define AWAITABLE_FLAG_H_

#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <asio/steady_timer.hpp>
#include <chrono>
#include <atomic>

class AwaitableFlag {
public:
    explicit AwaitableFlag(asio::any_io_executor executor, const std::chrono::time_point<std::chrono::steady_clock> timeout = asio::steady_timer::time_point::max())
        : m_executor(executor), m_timer(executor, timeout), m_flag(false) {}

    void Reset() {
        m_flag.store(false, std::memory_order_release);
    }

    void Signal() {
        m_flag.store(true, std::memory_order_release);
        m_timer.cancel();
    }

    asio::awaitable<void> Wait() {
        if (m_flag.load(std::memory_order_acquire))
            co_return;

        m_timer.expires_at(std::chrono::steady_clock::time_point::max());

        asio::error_code errorCode;
        co_await m_timer.async_wait(asio::redirect_error(asio::use_awaitable, errorCode));

        co_return;
    }

private:
    asio::any_io_executor m_executor;
    asio::steady_timer    m_timer;
    std::atomic<bool>     m_flag;

};

#endif//AWAITABLE_FLAG_H_