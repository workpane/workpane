#pragma once

#include "platform/InstalledFont.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace workpane::tests {

struct SystemRecord final {
    std::string locale{"en_US"};
    std::string timeZone{"Etc/UTC"};
    std::int64_t processId{4242};
    std::vector<platform::InstalledFont> fonts;
    std::map<std::string, int, std::less<>> zoneOffsets{{"Etc/UTC", 0}};
    std::vector<std::string> openedUrls;
    std::vector<std::filesystem::path> revealedPaths;
    std::vector<std::vector<std::string>> relaunches;
    nlohmann::json snapshot;
    nlohmann::json displays = nlohmann::json::array({{{"name", "Test Display"}, {"width", 1512}, {"height", 982}, {"scale", 2.0}, {"density", 254.0}, {"refreshRate", 120}, {"primary", true}}});
    bool inspectionFails{false};
    int inspections{0};
};

} // namespace workpane::tests
