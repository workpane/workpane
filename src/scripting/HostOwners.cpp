#include "scripting/HostOwners.h"

#include "localization/Localization.h"

#include <string>

namespace workpane::scripting {

Result<void> HostOwners::check(const PluginRegistry& plugins, std::string_view owner) {
    if (owner == localization::Localization::coreOwner || plugins.find(owner) != nullptr) {
        return Result<void>::success();
    }

    return Result<void>::failure({"plugin_unknown", "A host call names a plugin that is not registered", std::string(owner)});
}

} // namespace workpane::scripting
