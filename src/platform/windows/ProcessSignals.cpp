#include "platform/ProcessSignals.h"

namespace workpane::platform {

// Windows answers a write to a peer that went away as a failed write, so there is no signal to settle.
void ProcessSignals::ignoreBrokenPipes() {}

} // namespace workpane::platform
