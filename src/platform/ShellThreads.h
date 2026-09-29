#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace workpane::platform {

// Counts the shells whose threads still run, which outlive a closed terminal while they end its shell, so the host that started them waits for the last one before the product ends.
class ShellThreads final {
  public:
    void enter();
    void leave();
    void wait();

  private:
    std::mutex m_mutex;
    std::condition_variable m_left;
    std::size_t m_running{0};
};

} // namespace workpane::platform
