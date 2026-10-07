#pragma once

#include "Result.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace workpane::platform {

// Answers the zone of the system and the offsets of zones from the time zone database of the standard library, refusing a system without that database instead of throwing.
class TimeZoneDatabase final {
  public:
    [[nodiscard]] static Result<std::string> current();
    [[nodiscard]] static Result<int> offset(std::string_view zone, std::int64_t seconds);
};

} // namespace workpane::platform
