#include "execution/MainThreadQueue.h"

#include <utility>

namespace workpane::execution {

void MainThreadQueue::setWakeHandler(WakeHandler handler) {
    const std::scoped_lock lock(m_mutex);
    m_wake = std::move(handler);
}

void MainThreadQueue::post(Task task) {
    WakeHandler wake;

    {
        const std::scoped_lock lock(m_mutex);
        m_tasks.push_back(std::move(task));
        wake = m_wake;
    }

    // The frame loop may be sleeping in the window system, so it is woken outside the lock that the drain also takes.
    if (wake) {
        wake();
    }
}

// Runs every task posted before the call, so a task posting another one defers it to the next drain instead of starving the frame.
void MainThreadQueue::drain() {
    std::vector<Task> ready;

    {
        const std::scoped_lock lock(m_mutex);
        ready.swap(m_tasks);
    }

    for (auto& task : ready) {
        task();
    }
}

bool MainThreadQueue::empty() const {
    const std::scoped_lock lock(m_mutex);
    return m_tasks.empty();
}

} // namespace workpane::execution
