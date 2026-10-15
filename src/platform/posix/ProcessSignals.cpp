#include "platform/ProcessSignals.h"

#include <csignal>

namespace workpane::platform {

// A socket or a pipe whose peer went away raises a signal that ends the process, and neither OpenSSL nor a pipe written on Linux keeps it away, so the whole process ignores it and every write reports the broken pipe as an error.
void ProcessSignals::ignoreBrokenPipes() {
    std::signal(SIGPIPE, SIG_IGN);
}

} // namespace workpane::platform
