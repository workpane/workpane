#include "platform/ParentConsole.h"

namespace workpane::platform {

// The standard streams of a program on macOS and Linux already reach the terminal that started it.
void ParentConsole::attach() {}

} // namespace workpane::platform
