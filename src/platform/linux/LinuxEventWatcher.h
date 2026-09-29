#pragma once

#include <glib.h>

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace workpane::platform {

// Watches the descriptors and the next timeout of the main context of GLib on a thread of its own while the frame loop sleeps, and wakes the loop once GTK has something to run.
// The interface thread owns the context and runs every source itself, so the thread only waits on copies of the descriptors.
class LinuxEventWatcher final {
  public:
    using Wake = std::function<void()>;

    explicit LinuxEventWatcher(Wake wake);
    ~LinuxEventWatcher();

    LinuxEventWatcher(const LinuxEventWatcher&) = delete;
    LinuxEventWatcher& operator=(const LinuxEventWatcher&) = delete;

    void watch();
    void dispatch();

  private:
    static constexpr std::size_t initialDescriptors{16};

    void wait();
    void interrupt();

    Wake m_wake;
    GMainContext* m_context;
    int m_interrupt;
    std::vector<GPollFD> m_descriptors;
    gint m_priority{0};
    gint m_timeout{-1};
    bool m_prepared{false};
    std::mutex m_mutex;
    std::condition_variable m_changed;
    bool m_watching{false};
    bool m_interrupted{false};
    bool m_stopping{false};
    std::thread m_thread;
};

} // namespace workpane::platform
