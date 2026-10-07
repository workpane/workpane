#pragma once

#include "Result.h"
#include "platform/FallbackFace.h"
#include "platform/InstalledFont.h"
#include "platform/SystemServices.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::platform {

class NativeSystemServices final : public SystemServices {
  public:
    [[nodiscard]] Result<void> openUrl(std::string_view url) override;
    [[nodiscard]] Result<void> revealPath(const std::filesystem::path& path) override;
    [[nodiscard]] std::string locale() override;
    [[nodiscard]] std::filesystem::path home() override;
    [[nodiscard]] std::filesystem::path downloads() override;
    [[nodiscard]] Result<std::string> timeZone() override;
    [[nodiscard]] std::int64_t processId() override;
    [[nodiscard]] std::vector<InstalledFont> monospaceFonts() override;
    [[nodiscard]] std::vector<FallbackFace> fallbackFaces() override;
    [[nodiscard]] Result<int> zoneOffset(std::string_view zone, std::int64_t seconds) override;
    [[nodiscard]] Result<void> relaunch(const std::vector<std::string>& arguments) override;
};

} // namespace workpane::platform
