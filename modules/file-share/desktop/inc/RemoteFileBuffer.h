#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <span>
#include <vector>

class RemoteFileBuffer final {
  public:
    struct ReadResult {
        size_t bytes;
        bool finished;
        bool failed;
    };
    explicit RemoteFileBuffer(size_t capacity = 1024 * 1024) : m_data(std::max<size_t>(1, capacity)) {}

    bool TryWrite(std::span<const uint8_t> bytes) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_finished || bytes.size() > m_data.size() - m_size)
            return false;
        const size_t position = (m_begin + m_size) % m_data.size();
        const size_t first = std::min(bytes.size(), m_data.size() - position);
        std::memcpy(m_data.data() + position, bytes.data(), first);
        std::memcpy(m_data.data(), bytes.data() + first, bytes.size() - first);
        m_size += bytes.size();
        return true;
    }

    ReadResult Read(std::span<uint8_t> destination) {
        std::lock_guard<std::mutex> lock(m_mutex);
        const size_t count = std::min(destination.size(), m_size);
        const size_t first = std::min(count, m_data.size() - m_begin);
        if (count != 0) {
            std::memcpy(destination.data(), m_data.data() + m_begin, first);
            std::memcpy(destination.data() + first, m_data.data(), count - first);
            m_begin = (m_begin + count) % m_data.size();
            m_size -= count;
        }
        return {count, m_finished && m_size == 0, m_failed};
    }

    void Finish(bool success) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_finished = true;
        m_failed = m_failed || !success;
        if (m_failed)
            m_size = 0;
        m_cancelAction = {};
    }

    bool Cancelled() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_failed;
    }

    void SetCancelAction(std::function<void()> action) {
        bool cancelled;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            cancelled = m_failed;
            if (!cancelled && !m_finished)
                m_cancelAction = action;
        }
        if (cancelled && action)
            InvokeCancel(action);
    }

    void Cancel() {
        std::function<void()> action;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_finished && !m_failed)
                return;
            m_finished = true;
            m_failed = true;
            m_size = 0;
            action = std::move(m_cancelAction);
        }
        if (action)
            InvokeCancel(action);
    }

  private:
    static void InvokeCancel(const std::function<void()> &action) noexcept {
        try {
            action();
        } catch (...) {

        }
    }

    mutable std::mutex m_mutex;
    std::vector<uint8_t> m_data;
    size_t m_begin = 0;
    size_t m_size = 0;
    bool m_finished = false;
    bool m_failed = false;
    std::function<void()> m_cancelAction;
};
