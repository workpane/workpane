#include "platform/TimeZoneDatabase.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace workpane::platform {

Result<std::string> TimeZoneDatabase::current() {
    try {
        return Result<std::string>::success(std::string(std::chrono::current_zone()->name()));
    } catch (const std::runtime_error& failure) {
        return Result<std::string>::failure({"time_zone_unavailable", "The time zone of the system could not be read", failure.what()});
    }
}

// The zone is found among the zones and links of the database, so an unknown name is refused rather than thrown.
Result<int> TimeZoneDatabase::offset(std::string_view zone, std::int64_t seconds) {
    const std::chrono::tzdb* database = nullptr;

    try {
        database = &std::chrono::get_tzdb();
    } catch (const std::runtime_error& failure) {
        return Result<int>::failure({"time_zone_unavailable", "The time zone database of the system could not be read", failure.what()});
    }

    const auto link = std::ranges::find(database->links, zone, &std::chrono::time_zone_link::name);
    const std::string_view name = link == database->links.end() ? zone : link->target();
    const auto found = std::ranges::find(database->zones, name, &std::chrono::time_zone::name);

    if (found == database->zones.end()) {
        return Result<int>::failure({"time_zone_unknown", "The time zone is not known to the system", std::string(zone)});
    }

    const std::chrono::sys_info information = found->get_info(std::chrono::sys_seconds(std::chrono::seconds(seconds)));
    return Result<int>::success(static_cast<int>(information.offset.count()));
}

} // namespace workpane::platform
