#include "execution/WorkerPool.h"

#include <utility>

namespace workpane::execution {

WorkerPool::WorkerPool(std::size_t threadCount) {
    m_threads.reserve(threadCount);

    for (std::size_t index = 0; index < threadCount; ++index) {
        // clang-format off
        m_threads.emplace_back([this]() { run(); });
        // clang-format on
    }
}

WorkerPool::~WorkerPool() {
    shutdown();
}

void WorkerPool::post(Task task) {
    {
        const std::scoped_lock lock(m_mutex);

        if (m_closed) {
            return;
        }

        m_tasks.push_back(std::move(task));
    }

    m_ready.notify_one();
}

// Runs what is already queued and joins every thread, which only final teardown may do.
void WorkerPool::shutdown() {
    {
        const std::scoped_lock lock(m_mutex);
        m_closed = true;
    }

    m_ready.notify_all();

    for (auto& thread : m_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    m_threads.clear();
}

void WorkerPool::run() {
    while (true) {
        Task task;

        {
            std::unique_lock lock(m_mutex);
            // clang-format off
            m_ready.wait(lock, [this]() { return m_closed || !m_tasks.empty(); });
            // clang-format on

            if (m_tasks.empty()) {
                return;
            }

            task = std::move(m_tasks.front());
            m_tasks.pop_front();
        }

        task();
    }
}

} // namespace workpane::execution
