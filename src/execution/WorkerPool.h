#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace workpane::execution {

// Blocking work that is not a database statement runs here, so decoding and reading files never holds the interface thread.
class WorkerPool final {
  public:
    using Task = std::function<void()>;

    explicit WorkerPool(std::size_t threadCount);
    ~WorkerPool();

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    void post(Task task);
    void shutdown();

  private:
    void run();

    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::deque<Task> m_tasks;
    std::vector<std::thread> m_threads;
    bool m_closed{false};
};

} // namespace workpane::execution
