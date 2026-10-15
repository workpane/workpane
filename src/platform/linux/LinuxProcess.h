#pragma once

#include "Result.h"

#include <string>
#include <string_view>
#include <vector>

namespace workpane::platform {

// Starts a system program in a session of its own with its three streams alone, reporting its failure when the caller needs to know it succeeded.
class LinuxProcess final {
  public:
    [[nodiscard]] static Result<void> spawn(const std::vector<std::string>& command, bool reap, std::string_view failureCode);

  private:
    static constexpr int statusWaitMilliseconds{2000};
    static constexpr int statusPollMilliseconds{20};
};

} // namespace workpane::platform
