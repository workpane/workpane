#include "platform/linux/LinuxEventWatcher.h"

#include <sys/eventfd.h>
#include <unistd.h>

#include <cstdint>
#include <tuple>
#include <utility>

namespace workpane::platform {

// The interface thread owns the main context from here on, which lets it prepare and check the context in steps of its own.
LinuxEventWatcher::LinuxEventWatcher(Wake wake) : m_wake(std::move(wake)), m_context(g_main_context_default()), m_interrupt(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)), m_descriptors(initialDescriptors) {
    g_main_context_acquire(m_context);
    m_thread = std::thread(&LinuxEventWatcher::wait, this);
}

LinuxEventWatcher::~LinuxEventWatcher() {
    {
        const std::lock_guard lock(m_mutex);
        m_stopping = true;
        interrupt();
    }

    m_changed.notify_all();
    m_thread.join();
    close(m_interrupt);
    g_main_context_release(m_context);
}

// The context tells which descriptors it waits on and how long until its next timeout, and a context with a source already ready is due at once.
void LinuxEventWatcher::watch() {
    if (m_prepared) {
        return;
    }

    const bool ready = g_main_context_prepare(m_context, &m_priority) == TRUE;
    gint timeout = -1;
    gint count = g_main_context_query(m_context, m_priority, &timeout, m_descriptors.data(), static_cast<gint>(m_descriptors.size()));

    if (static_cast<std::size_t>(count) > m_descriptors.size()) {
        m_descriptors.resize(static_cast<std::size_t>(count));
        count = g_main_context_query(m_context, m_priority, &timeout, m_descriptors.data(), count);
    }

    m_prepared = true;

    {
        const std::lock_guard lock(m_mutex);
        m_descriptors.resize(static_cast<std::size_t>(count));
        m_timeout = ready ? 0 : timeout;
        m_watching = true;
    }

    m_changed.notify_all();
}

// Runs what GTK has ready once the loop woke, the sources the watch saw ready first and then every other one pending.
void LinuxEventWatcher::dispatch() {
    if (m_prepared) {
        std::unique_lock lock(m_mutex);

        if (m_watching) {
            interrupt();
            // clang-format off
            m_changed.wait(lock, [this]() { return !m_watching; });
            // clang-format on
        }

        lock.unlock();
        m_prepared = false;

        if (g_main_context_check(m_context, m_priority, m_descriptors.data(), static_cast<gint>(m_descriptors.size())) == TRUE) {
            g_main_context_dispatch(m_context);
        }
    }

    while (g_main_context_pending(m_context) == TRUE) {
        g_main_context_iteration(m_context, FALSE);
    }
}

// The thread waits on the descriptors of one watch at a time, and wakes the loop when one of them is ready or the timeout passed, but not when the loop itself ended the watch.
void LinuxEventWatcher::wait() {
    std::unique_lock lock(m_mutex);

    while (true) {
        // clang-format off
        m_changed.wait(lock, [this]() { return m_watching || m_stopping; });
        // clang-format on

        if (m_stopping) {
            return;
        }

        std::vector<GPollFD> polled = m_descriptors;
        polled.push_back({m_interrupt, G_IO_IN, 0});
        const gint timeout = m_timeout;
        lock.unlock();
        std::ignore = g_poll(polled.data(), static_cast<guint>(polled.size()), timeout);
        lock.lock();
        std::uint64_t drained = 0;
        std::ignore = read(m_interrupt, &drained, sizeof(drained));

        for (std::size_t index = 0; index < m_descriptors.size(); ++index) {
            m_descriptors[index].revents = polled[index].revents;
        }

        m_watching = false;

        if (!std::exchange(m_interrupted, false)) {
            m_wake();
        }

        m_changed.notify_all();
    }
}

// Ends the watch in progress, which the caller does while it holds the lock, so the thread sees the interruption after its wait returns.
void LinuxEventWatcher::interrupt() {
    if (!m_watching) {
        return;
    }

    const std::uint64_t one = 1;
    m_interrupted = true;
    std::ignore = write(m_interrupt, &one, sizeof(one));
}

} // namespace workpane::platform
