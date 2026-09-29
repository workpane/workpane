#include "platform/posix/ExitDescriptor.h"

#include <tuple>

namespace workpane::platform {

// The system revokes the terminal of a session whose leader ended, so the end of a shell already shows as the end of its terminal and needs no descriptor.
int ExitDescriptor::open(pid_t child) {
    std::ignore = child;

    return -1;
}

} // namespace workpane::platform
