#pragma once

#include "Result.h"
#include "process/Process.h"
#include "process/ProcessEvents.h"
#include "process/ProcessLaunch.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace workpane::process {

// Starts programs for plugins and finds their executables, so no host function names a platform.
class ProcessLauncher {
  public:
    virtual ~ProcessLauncher() = default;

    [[nodiscard]] virtual Result<std::unique_ptr<Process>> start(const ProcessLaunch& launch, ProcessEvents events) = 0;
    [[nodiscard]] virtual std::optional<std::filesystem::path> find(std::string_view name, const std::vector<std::filesystem::path>& directories) const = 0;
};

} // namespace workpane::process
