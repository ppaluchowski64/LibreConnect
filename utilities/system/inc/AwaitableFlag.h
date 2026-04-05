#ifndef AWAITABLE_FLAG_H_
#define AWAITABLE_FLAG_H_

#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <asio/steady_timer.hpp>
#include <chrono>
#include <atomic>

class AwaitableFlag {
public:
    enum class Result : bool {
        TIMEOUT,
        SUCCESS
    };

    explicit AwaitableFlag(const asio::any_io_executor& executor)
        : m_executor(executor), m_timer(executor), m_flag(false) {}

    void Reset() {
        m_flag.store(false, std::memory_order_release);
    }

    void Signal() {
        m_flag.store(true, std::memory_order_release);
        m_timer.cancel();
    }

    asio::awaitable<Result> Wait(const std::chrono::time_point<std::chrono::steady_clock> timeout = asio::steady_timer::time_point::max()) {
        if (m_flag.load(std::memory_order_acquire))
            co_return Result::SUCCESS;

        m_timer.expires_at(timeout);

        asio::error_code errorCode;
        co_await m_timer.async_wait(asio::redirect_error(asio::use_awaitable, errorCode));

        if (errorCode == asio::error::operation_aborted && m_flag.load(std::memory_order_acquire)) {
            co_return Result::SUCCESS;
        }

        co_return Result::TIMEOUT;
    }

    template <typename Rep, typename Period>
    asio::awaitable<Result> WaitFor(const std::chrono::duration<Rep, Period>& timeout) {
        const auto timeoutDuration = std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout);
        co_return co_await Wait(std::chrono::steady_clock::now() + timeoutDuration);
    }

private:
    asio::any_io_executor m_executor;
    asio::steady_timer    m_timer;
    std::atomic<bool>     m_flag;

};

#endif//AWAITABLE_FLAG_H_
