#pragma once

#include "Result.h"
#include "platform/SystemServices.h"
#include "support/SystemRecord.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::tests {

class FakeSystemServices final : public platform::SystemServices {
  public:
    explicit FakeSystemServices(std::shared_ptr<SystemRecord> record);

    [[nodiscard]] Result<void> openUrl(std::string_view url) override;
    [[nodiscard]] Result<void> revealPath(const std::filesystem::path& path) override;
    [[nodiscard]] std::string locale() override;
    [[nodiscard]] std::filesystem::path home() override;
    [[nodiscard]] std::filesystem::path downloads() override;
    [[nodiscard]] Result<std::string> timeZone() override;
    [[nodiscard]] std::int64_t processId() override;
    [[nodiscard]] std::vector<platform::InstalledFont> monospaceFonts() override;
    [[nodiscard]] Result<int> zoneOffset(std::string_view zone, std::int64_t seconds) override;
    [[nodiscard]] Result<void> relaunch(const std::vector<std::string>& arguments) override;

  private:
    std::shared_ptr<SystemRecord> m_record;
};

} // namespace workpane::tests
