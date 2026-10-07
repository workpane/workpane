#pragma once

#include "Result.h"
#include "process/Process.h"
#include "process/ProcessEvents.h"
#include "process/ProcessLaunch.h"
#include "process/ProcessLauncher.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace workpane::platform {

// Starts programs with pipes on the running platform and finds executables on its search path.
class NativeProcesses final : public process::ProcessLauncher {
  public:
    [[nodiscard]] Result<std::unique_ptr<process::Process>> start(const process::ProcessLaunch& launch, process::ProcessEvents events) override;
    [[nodiscard]] std::optional<std::filesystem::path> find(std::string_view name, const std::vector<std::filesystem::path>& directories) const override;

  private:
    [[nodiscard]] static std::vector<std::filesystem::path> searchPath();
    [[nodiscard]] static std::optional<std::filesystem::path> executable(const std::filesystem::path& candidate);
};

} // namespace workpane::platform
