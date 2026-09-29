#pragma once

#include "process/ProcessLaunch.h"

#include <string>
#include <string_view>
#include <vector>

namespace workpane::process {

// The environment a program starts with: the inherited entries without the cleared ones, and the added variables replacing any of the same name.
class ProcessEnvironment final {
  public:
    [[nodiscard]] static std::vector<std::string> build(const std::vector<std::string>& inherited, const ProcessLaunch& launch, bool caseInsensitive);

  private:
    [[nodiscard]] static bool sameName(std::string_view first, std::string_view second, bool caseInsensitive);
};

} // namespace workpane::process
