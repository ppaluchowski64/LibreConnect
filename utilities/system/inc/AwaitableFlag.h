#ifndef AWAITABLE_FLAG_H_
#define AWAITABLE_FLAG_H_

#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <asio/steady_timer.hpp>
#include <chrono>
#include <atomic>
#include <algorithm>

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
        const bool hasTimeout = timeout != asio::steady_timer::time_point::max();
        constexpr auto POLL_INTERVAL = std::chrono::milliseconds(250);

        while (true) {
            if (TryConsume()) {
                co_return Result::SUCCESS;
            }

            const auto now = std::chrono::steady_clock::now();
            if (hasTimeout && now >= timeout) {
                co_return Result::TIMEOUT;
            }

            const auto nextWakeTime = hasTimeout
                ? std::min(timeout, now + POLL_INTERVAL)
                : now + POLL_INTERVAL;
            m_timer.expires_at(nextWakeTime);

            asio::error_code errorCode;
            co_await m_timer.async_wait(asio::redirect_error(asio::use_awaitable, errorCode));

            if (errorCode != asio::error::operation_aborted && hasTimeout && std::chrono::steady_clock::now() >= timeout) {
                co_return Result::TIMEOUT;
            }
        }
    }

    template <typename Rep, typename Period>
    asio::awaitable<Result> WaitFor(const std::chrono::duration<Rep, Period>& timeout) {
        const auto timeoutDuration = std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout);
        co_return co_await Wait(std::chrono::steady_clock::now() + timeoutDuration);
    }

private:
    bool TryConsume() {
        return m_flag.exchange(false, std::memory_order_acq_rel);
    }

    asio::any_io_executor m_executor;
    asio::steady_timer    m_timer;
    std::atomic<bool>     m_flag;

};

#endif//AWAITABLE_FLAG_H_
