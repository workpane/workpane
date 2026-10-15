#pragma once

#include "Result.h"
#include "scripting/PluginRegistry.h"

#include <string_view>

namespace workpane::scripting {

// Every caller names the plugin it speaks for, which must be a registered plugin or the core owner itself.
class HostOwners final {
  public:
    [[nodiscard]] static Result<void> check(const PluginRegistry& plugins, std::string_view owner);
};

} // namespace workpane::scripting
