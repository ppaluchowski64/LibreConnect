#include <ThreadPool.h>
#include <DebugLog.h>

ThreadPool* ThreadPool::s_instance{nullptr};
std::once_flag ThreadPool::s_flag{};

void ThreadPool::Initialize() {
    Debug::Log("ThreadPool: initialize");
    s_instance = new ThreadPool();
    s_instance->StartImpl();
}

void ThreadPool::StartImpl() {
    std::lock_guard<std::mutex> lock(s_instance->m_mutex);

    if (!s_instance->m_threads.empty()) {
        Debug::Log("ThreadPool: start skipped (threads already running)");
        return;
    }

    if (s_instance->m_context.stopped()) {
        Debug::Log("ThreadPool: restarting IO context");
        s_instance->m_context.restart();
    }

    if (!s_instance->m_workGuard.has_value() || !s_instance->m_workGuard->owns_work()) {
        Debug::Log("ThreadPool: recreating work guard");
        s_instance->m_workGuard.emplace(asio::make_work_guard(s_instance->m_context));
    }

    unsigned int threadCount = std::thread::hardware_concurrency();
    if (threadCount == 0) {
        Debug::LogWarning("ThreadPool: hardware_concurrency returned 0, using 1 thread");
        threadCount = 1;
    }

    s_instance->m_threads.reserve(threadCount);
    Debug::Log("ThreadPool: starting {} worker threads", threadCount);

    for (int i = 0; i < threadCount; i++) {
        s_instance->m_threads.emplace_back([]() {
            s_instance->m_context.run();
        });
    }
}

ThreadPool::ThreadPool()
    : m_workGuard(std::in_place, asio::make_work_guard(m_context)) {}

IOContext& ThreadPool::GetContext() {
    std::call_once(s_flag, Initialize);
    return s_instance->m_context;
}

void ThreadPool::Stop() {
    std::call_once(s_flag, Initialize);
    std::lock_guard<std::mutex> lock(s_instance->m_mutex);

    Debug::Log("ThreadPool: stopping ({} threads)", s_instance->m_threads.size());
    s_instance->m_workGuard.reset();
    s_instance->m_context.stop();

    for (auto& thread : s_instance->m_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    s_instance->m_threads.clear();
    Debug::Log("ThreadPool: stopped");
}

void ThreadPool::Start() {
    std::call_once(s_flag, Initialize);
    Debug::Log("ThreadPool: start requested");
    StartImpl();
}
