#include "platform/ShellThreads.h"

namespace workpane::platform {

void ShellThreads::enter() {
    const std::lock_guard lock(m_mutex);
    ++m_running;
}

// The count falls under the lock the host reads it with, so the host never misses the last shell.
void ShellThreads::leave() {
    {
        const std::lock_guard lock(m_mutex);
        --m_running;
    }

    m_left.notify_all();
}

void ShellThreads::wait() {
    std::unique_lock lock(m_mutex);
    // clang-format off
    m_left.wait(lock, [this]() { return m_running == 0; });
    // clang-format on
}

} // namespace workpane::platform
