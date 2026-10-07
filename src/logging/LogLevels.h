#pragma once

#include <optional>
#include <string_view>

namespace workpane::logging {

enum class LogLevel { Debug, Info, Warning, Error };

class LogLevels final {
  public:
    [[nodiscard]] static std::string_view name(LogLevel level);
    [[nodiscard]] static std::optional<LogLevel> parse(std::string_view name);
};

} // namespace workpane::logging
