#include "logging/LogLevels.h"

namespace workpane::logging {

std::string_view LogLevels::name(LogLevel level) {
    switch (level) {
    case LogLevel::Debug:
        return "debug";
    case LogLevel::Info:
        return "info";
    case LogLevel::Warning:
        return "warning";
    case LogLevel::Error:
        return "error";
    }

    return "error";
}

std::optional<LogLevel> LogLevels::parse(std::string_view name) {
    if (name == "debug") {
        return LogLevel::Debug;
    }

    if (name == "info") {
        return LogLevel::Info;
    }

    if (name == "warning") {
        return LogLevel::Warning;
    }

    if (name == "error") {
        return LogLevel::Error;
    }

    return std::nullopt;
}

} // namespace workpane::logging
