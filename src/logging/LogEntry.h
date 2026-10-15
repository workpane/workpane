#pragma once

#include "logging/LogLevels.h"
#include "time/TimestampHelper.h"

#include <nlohmann/json.hpp>

#include <string>

namespace workpane::logging {

struct LogEntry final {
    time::Instant timestamp;
    std::string source;
    LogLevel level{LogLevel::Info};
    std::string category;
    std::string message;
    nlohmann::json details;
};

} // namespace workpane::logging
