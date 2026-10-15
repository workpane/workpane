#pragma once

#include <functional>
#include <mutex>
#include <vector>

namespace workpane::execution {

// Work posted from any thread runs on the interface thread the next time the frame loop drains the queue.
class MainThreadQueue final {
  public:
    using Task = std::function<void()>;
    using WakeHandler = std::function<void()>;

    void setWakeHandler(WakeHandler handler);
    void post(Task task);
    void drain();
    [[nodiscard]] bool empty() const;

  private:
    mutable std::mutex m_mutex;
    std::vector<Task> m_tasks;
    WakeHandler m_wake;
};

} // namespace workpane::execution
