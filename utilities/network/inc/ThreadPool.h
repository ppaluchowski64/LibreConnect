#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <thread>
#include <utility>

#include <asio.hpp>
#include <AsioCommon.h>

class ThreadPool {
public:
    explicit ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    static IOContext& GetContext();

    template<typename T>
    static void Post(std::function<T>&& function) {
        std::call_once(s_flag, Initialize);
        asio::post(s_instance->m_context, std::forward<std::function<T>>(function));
    }

    template<typename T>
    static void Post(const std::function<T>& function) {
        std::call_once(s_flag, Initialize);
        asio::post(s_instance->m_context, function);
    }

private:
    static void Initialize();

    static ThreadPool* s_instance;
    static std::once_flag s_flag;

    IOContext m_context;
    IOWorkGuard m_workGuard;
    std::vector<std::thread> m_threads;

};

#endif //THREAD_POOL_H