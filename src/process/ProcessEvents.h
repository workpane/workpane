#pragma once

#include "process/ProcessEnd.h"
#include "process/ProcessStream.h"

#include <functional>
#include <string>

namespace workpane::process {

// Where a running program reports what it wrote and, after its last output, how it ended, both called from the thread that serves it.
struct ProcessEvents final {
    std::function<void(ProcessStream stream, std::string bytes)> output;
    std::function<void(ProcessEnd end)> exited;
};

} // namespace workpane::process
