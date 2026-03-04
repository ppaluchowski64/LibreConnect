#include <ThreadPool.h>

ThreadPool* ThreadPool::s_instance{nullptr};
std::once_flag ThreadPool::s_flag{};

void ThreadPool::Initialize() {
    s_instance = new ThreadPool();
}

ThreadPool::ThreadPool() : m_workGuard(asio::make_work_guard(m_context)){
    Start();
}

IOContext& ThreadPool::GetContext() {
    std::call_once(s_flag, Initialize);
    return s_instance->m_context;
}

void ThreadPool::Stop() {
    std::call_once(s_flag, Initialize);

    s_instance->m_workGuard.reset();
    s_instance->m_context.stop();

    for (auto& thread : s_instance->m_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    s_instance->m_threads.clear();
}

void ThreadPool::Start() {
    std::call_once(s_flag, Initialize);
    unsigned int threadCount = std::thread::hardware_concurrency();
    if (threadCount == 0) threadCount = 1;

    s_instance->m_threads.reserve(threadCount);

    for (int i = 0; i < threadCount; i++) {
        s_instance->m_threads.emplace_back([]() {
            s_instance->m_context.run();
        });
    }
}
