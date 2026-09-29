#pragma once

#include "Result.h"
#include "platform/InstalledFont.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::platform {

// What the product asks of the operating system outside its own window, behind one interface so the suite replaces it.
class SystemServices {
  public:
    virtual ~SystemServices() = default;

    [[nodiscard]] virtual Result<void> openUrl(std::string_view url) = 0;
    [[nodiscard]] virtual Result<void> revealPath(const std::filesystem::path& path) = 0;
    [[nodiscard]] virtual std::string locale() = 0;
    [[nodiscard]] virtual std::filesystem::path home() = 0;
    [[nodiscard]] virtual std::filesystem::path downloads() = 0;
    [[nodiscard]] virtual Result<std::string> timeZone() = 0;
    [[nodiscard]] virtual std::int64_t processId() = 0;
    [[nodiscard]] virtual std::vector<InstalledFont> monospaceFonts() = 0;
    [[nodiscard]] virtual Result<int> zoneOffset(std::string_view zone, std::int64_t seconds) = 0;
    [[nodiscard]] virtual Result<void> relaunch(const std::vector<std::string>& arguments) = 0;
};

} // namespace workpane::platform
