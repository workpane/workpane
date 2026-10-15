#pragma once

#include "Result.h"

#include <string>

namespace workpane::tests {

class WindowsAccountHelper final {
  public:
    [[nodiscard]] static Result<std::string> current();

  private:
    struct Identity;
};

} // namespace workpane::tests
