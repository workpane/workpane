#include "logging/LogService.h"

#include "BuildInfo.h"

#include <cstdio>
#include <iterator>
#include <utility>
#include <vector>

namespace workpane::logging {

void LogService::write(LogLevel level, std::string source, std::string category, std::string message, nlohmann::json details) {
    write(LogEntry{time::TimestampHelper::now(), std::move(source), level, std::move(category), std::move(message), std::move(details)});
}

void LogService::write(LogEntry entry) {
    // A debug build mirrors every entry to the console, because that is where a developer looks before any view exists.
    if constexpr (app::BuildInfo::debugBuild) {
        const std::string details = entry.details.empty() ? std::string() : " " + entry.details.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        std::fprintf(stderr, "[%s] %s %s/%s: %s%s\n", time::TimestampHelper::storedTimestamp(entry.timestamp).c_str(), std::string(LogLevels::name(entry.level)).c_str(), entry.source.c_str(), entry.category.c_str(), entry.message.c_str(), details.c_str());
    }

    {
        const std::scoped_lock lock(m_mutex);

        if (!m_delivering && m_pending.size() == pendingCapacity) {
            m_pending.pop_front();
        }

        m_pending.push_back(std::move(entry));

        if (!m_delivering || m_notified) {
            return;
        }

        m_notified = true;
    }

    notify();
}

// The consumer hears that entries wait once until it takes them, and every entry written before it existed waits for it.
void LogService::setDelivery(Notify notify) {
    {
        const std::scoped_lock notifying(m_notifying);
        m_notify = std::move(notify);
    }

    {
        const std::scoped_lock lock(m_mutex);
        m_delivering = true;

        if (m_pending.empty() || m_notified) {
            return;
        }

        m_notified = true;
    }

    this->notify();
}

// Once the delivery is cleared its consumer is never called again, and entries wait for the next one.
void LogService::clearDelivery() {
    {
        const std::scoped_lock lock(m_mutex);
        m_delivering = false;
        m_notified = false;
    }

    const std::scoped_lock notifying(m_notifying);
    m_notify = nullptr;
}

std::vector<LogEntry> LogService::take() {
    const std::scoped_lock lock(m_mutex);
    std::vector<LogEntry> taken(std::make_move_iterator(m_pending.begin()), std::make_move_iterator(m_pending.end()));
    m_pending.clear();
    m_notified = false;

    return taken;
}

// The consumer is called under a lock of its own that clearing the delivery takes too, and an entry it writes itself while it is called only waits to be taken.
void LogService::notify() {
    const std::scoped_lock notifying(m_notifying);

    if (m_notify) {
        m_notify();
    }
}

} // namespace workpane::logging
