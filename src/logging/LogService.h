#pragma once

#include "logging/LogEntry.h"
#include "logging/LogLevels.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace workpane::logging {

// Every entry of the product passes through here from any thread and waits until its consumer takes it, and before a consumer exists only the newest entries are kept.
class LogService final {
  public:
    using Notify = std::function<void()>;

    void write(LogLevel level, std::string source, std::string category, std::string message, nlohmann::json details = nlohmann::json::object());
    void setDelivery(Notify notify);
    void clearDelivery();
    [[nodiscard]] std::vector<LogEntry> take();

  private:
    static constexpr std::size_t pendingCapacity{2000};

    void write(LogEntry entry);
    void notify();

    std::mutex m_mutex;
    std::mutex m_notifying;
    std::deque<LogEntry> m_pending;
    Notify m_notify;
    bool m_delivering{false};
    bool m_notified{false};
};

} // namespace workpane::logging
