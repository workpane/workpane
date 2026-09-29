#pragma once

#include "Error.h"
#include "process/ProcessEvents.h"
#include "process/ProcessLaunch.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::tests {

// The programs a test started: their launches, what each was sent and whether it was stopped, the events a test raises for them and the executables the search finds.
// A responder plays a program, answering what the plugin writes by raising output through the events of that program.
struct ProcessRecord final {
    std::vector<process::ProcessLaunch> launches;
    std::vector<process::ProcessEvents> events;
    std::vector<std::string> inputs;
    std::vector<bool> stopped;
    std::vector<bool> ended;
    std::map<std::string, std::filesystem::path> executables;
    std::function<void(std::size_t program, std::string_view written)> responder;
    std::optional<Error> refusal;
};

} // namespace workpane::tests
