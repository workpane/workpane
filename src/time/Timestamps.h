#pragma once

#include "time/LocalMoment.h"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace workpane::time {

using Clock = std::chrono::system_clock;
using Instant = std::chrono::time_point<Clock, std::chrono::milliseconds>;

// Every stored moment is UTC ISO 8601 with milliseconds, and the local zone is applied only when a moment is presented.
class Timestamps final {
  public:
    [[nodiscard]] static Instant now();
    [[nodiscard]] static std::string storedTimestamp(Instant instant);
    [[nodiscard]] static std::optional<Instant> parseStoredTimestamp(std::string_view text);
    [[nodiscard]] static std::optional<std::string> localPresentation(Instant instant);
    [[nodiscard]] static std::optional<LocalMoment> localMoment(Instant instant);

  private:
    [[nodiscard]] static bool readNumber(std::string_view text, std::size_t offset, std::size_t length, int& out);
    [[nodiscard]] static bool localCalendar(std::time_t seconds, std::tm& out);
};

} // namespace workpane::time
