#pragma once

#include "Result.h"
#include "process/Process.h"
#include "process/ProcessEvents.h"
#include "process/ProcessLaunch.h"
#include "process/ProcessLauncher.h"
#include "support/ProcessRecord.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace workpane::tests {

// Starts recorded programs instead of real ones and finds the executables the record names.
class FakeProcessLauncher final : public process::ProcessLauncher {
  public:
    explicit FakeProcessLauncher(std::shared_ptr<ProcessRecord> record);

    [[nodiscard]] Result<std::unique_ptr<process::Process>> start(const process::ProcessLaunch& launch, process::ProcessEvents events) override;
    [[nodiscard]] std::optional<std::filesystem::path> find(std::string_view name, const std::vector<std::filesystem::path>& directories) const override;

  private:
    std::shared_ptr<ProcessRecord> m_record;
};

} // namespace workpane::tests
