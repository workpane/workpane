#pragma once

#include "Result.h"
#include "execution/MainThreadQueue.h"
#include "persistence/Database.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace workpane::persistence {

// Every statement runs on one serialized worker that owns the connection, and every answer returns on the interface thread.
class DatabaseExecutor final {
  public:
    using Work = std::function<void(Database&)>;

    DatabaseExecutor(Database database, execution::MainThreadQueue& mainThread);
    ~DatabaseExecutor();

    DatabaseExecutor(const DatabaseExecutor&) = delete;
    DatabaseExecutor& operator=(const DatabaseExecutor&) = delete;

    template <typename T> void submit(std::function<Result<T>(Database&)> work, std::function<void(Result<T>)> done) {
        // clang-format off
        enqueue([work = std::move(work), done = std::move(done), mainThread = &m_mainThread](Database& database) mutable {
            auto result = std::make_shared<Result<T>>(work(database));
            mainThread->post([done = std::move(done), result]() { done(std::move(*result)); });
        });
        // clang-format on
    }

    void shutdown();

  private:
    static constexpr std::chrono::milliseconds drainBound{2000};

    void enqueue(Work work);
    void run();

    Database m_database;
    execution::MainThreadQueue& m_mainThread;
    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::condition_variable m_finished;
    std::deque<Work> m_queue;
    bool m_closed{false};
    bool m_done{false};
    std::thread m_worker;
};

} // namespace workpane::persistence
