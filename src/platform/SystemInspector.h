#pragma once

#include "Result.h"

#include <nlohmann/json.hpp>

namespace workpane::platform {

// Describes the machine the product runs on in one snapshot per call, so a plugin shows the hardware without reaching it.
class SystemInspector {
  public:
    virtual ~SystemInspector() = default;

    [[nodiscard]] virtual Result<nlohmann::json> inspect() = 0;
};

} // namespace workpane::platform
