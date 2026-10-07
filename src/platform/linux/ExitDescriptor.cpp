#include "platform/posix/ExitDescriptor.h"

#include <sys/syscall.h>
#include <unistd.h>

namespace workpane::platform {

// A process descriptor is readable once its process ended and never reaches a program the product starts.
int ExitDescriptor::open(pid_t child) {
    return static_cast<int>(::syscall(SYS_pidfd_open, child, 0));
}

} // namespace workpane::platform
