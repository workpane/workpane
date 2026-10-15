#pragma once

#include "Result.h"
#include "platform/SystemInspector.h"

#include <nlohmann/json.hpp>

namespace workpane::platform {

// Collects the snapshot from the operating system of the running platform, blocking its caller for the short utilization sample it takes.
class NativeSystemInspector final : public SystemInspector {
  public:
    [[nodiscard]] Result<nlohmann::json> inspect() override;

  private:
    [[nodiscard]] static nlohmann::json operatingSystem();
    [[nodiscard]] static nlohmann::json processors();
    [[nodiscard]] static nlohmann::json processorUsage();
    [[nodiscard]] static nlohmann::json memory();
    [[nodiscard]] static nlohmann::json graphics();
    [[nodiscard]] static nlohmann::json mainboard();
    [[nodiscard]] static nlohmann::json disks();
    [[nodiscard]] static nlohmann::json batteries();
    [[nodiscard]] static nlohmann::json networkInterfaces();
};

} // namespace workpane::platform
