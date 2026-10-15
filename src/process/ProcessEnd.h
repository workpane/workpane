#pragma once

namespace workpane::process {

// How a program ended: the code it reported and whether it crashed rather than ending on its own, which is a signal on POSIX and a failure status on Windows.
struct ProcessEnd final {
    int code{0};
    bool crashed{false};
};

} // namespace workpane::process
