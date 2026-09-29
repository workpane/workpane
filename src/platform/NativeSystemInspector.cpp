#include "platform/NativeSystemInspector.h"

#include <utility>

namespace workpane::platform {

Result<nlohmann::json> NativeSystemInspector::inspect() {
    nlohmann::json snapshot;
    snapshot["os"] = operatingSystem();
    snapshot["processors"] = processors();
    snapshot["processorUsage"] = processorUsage();
    snapshot["memory"] = memory();
    snapshot["graphics"] = graphics();
    snapshot["mainboard"] = mainboard();
    snapshot["disks"] = disks();
    snapshot["batteries"] = batteries();
    snapshot["networkInterfaces"] = networkInterfaces();

    return Result<nlohmann::json>::success(std::move(snapshot));
}

} // namespace workpane::platform
