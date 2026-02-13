#include <ThreadPool.h>

ThreadPool* ThreadPool::s_instance{nullptr};
std::once_flag ThreadPool::s_flag{};

void ThreadPool::Initialize() {
    s_instance = new ThreadPool();
}

ThreadPool::ThreadPool() : m_workGuard(asio::make_work_guard(m_context)){
    unsigned int threadCount = std::thread::hardware_concurrency();
    if (threadCount == 0) threadCount = 1;

    m_threads.reserve(threadCount);

    for (int i = 0; i < threadCount; i++) {
        m_threads.emplace_back([this]() {
            m_context.run();
        });
    }
}

IOContext& ThreadPool::GetContext() {
    std::call_once(s_flag, Initialize);
    return s_instance->m_context;
}
