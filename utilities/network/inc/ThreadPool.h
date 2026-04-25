#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <thread>
#include <utility>
#include <future>
#include <functional>
#include <type_traits>
#include <mutex>
#include <optional>

#include <asio.hpp>
#include <AsioCommon.h>

class ThreadPool {
public:
    explicit ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    static IOContext& GetContext();
    static void Stop();
    static void Start();

    template<typename T>
    static void Post(T&& function) {
        std::call_once(s_flag, Initialize);
        asio::post(s_instance->m_context, std::forward<T>(function));
    }

    template<typename T>
    static void Post(const std::function<T>& function) {
        std::call_once(s_flag, Initialize);
        asio::post(s_instance->m_context, function);
    }

    template<typename T>
    static std::future<std::invoke_result_t<std::decay_t<T>&>> PostFuture(T&& function) {
        std::call_once(s_flag, Initialize);

        using ReturnType = std::invoke_result_t<std::decay_t<T>&>;
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(std::forward<T>(function));
        std::future<ReturnType> future = task->get_future();

        asio::post(s_instance->m_context, [task]() mutable {
            (*task)();
        });

        return future;
    }

    template<class Rep, class Period>
    static asio::awaitable<void> AsyncYieldFor(std::chrono::duration<Rep, Period> duration) {
        asio::steady_timer timer(co_await asio::this_coro::executor);
        timer.expires_after(std::chrono::duration_cast<asio::steady_timer::duration>(duration));
        co_await timer.async_wait(asio::use_awaitable);
    }


private:
    static void Initialize();
    static void StartImpl();

    static ThreadPool* s_instance;
    static std::once_flag s_flag;

    IOContext m_context;
    std::optional<IOWorkGuard> m_workGuard;
    std::mutex m_mutex;
    std::vector<std::thread> m_threads;

};

#endif //THREAD_POOL_H
