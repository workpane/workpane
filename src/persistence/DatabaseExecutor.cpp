#include "persistence/DatabaseExecutor.h"

#include <utility>

namespace workpane::persistence {

DatabaseExecutor::DatabaseExecutor(Database database, execution::MainThreadQueue& mainThread) : m_database(std::move(database)), m_mainThread(mainThread) {
    // clang-format off
    m_worker = std::thread([this]() { run(); });
    // clang-format on
}

DatabaseExecutor::~DatabaseExecutor() {
    shutdown();
}

// Runs what is already queued rather than discarding it, joins the worker and closes the connection, so the data directory can be released and reused by another process at once.
// A plugin statement that still holds the worker past the bound is interrupted, so quitting never waits on it.
void DatabaseExecutor::shutdown() {
    {
        std::unique_lock lock(m_mutex);
        m_closed = true;
        m_ready.notify_all();

        // clang-format off
        if (!m_finished.wait_for(lock, drainBound, [this]() { return m_done; })) {
            m_database.interrupt();
        }
        // clang-format on
    }

    if (m_worker.joinable()) {
        m_worker.join();
    }

    m_database.close();
}

void DatabaseExecutor::enqueue(Work work) {
    {
        const std::scoped_lock lock(m_mutex);

        if (m_closed) {
            return;
        }

        m_queue.push_back(std::move(work));
    }

    m_ready.notify_one();
}

void DatabaseExecutor::run() {
    while (true) {
        Work work;

        {
            std::unique_lock lock(m_mutex);
            // clang-format off
            m_ready.wait(lock, [this]() { return m_closed || !m_queue.empty(); });
            // clang-format on

            if (m_queue.empty()) {
                m_done = true;
                m_finished.notify_all();
                return;
            }

            work = std::move(m_queue.front());
            m_queue.pop_front();
        }

        work(m_database);
    }
}

} // namespace workpane::persistence
