#pragma once

#include <sys/types.h>

namespace workpane::platform {

// Opens a descriptor that turns readable once a child process ends, so a thread waiting on other descriptors hears of that end in the same wait.
// A system that needs none answers minus one, and the child is then known to have ended when its streams close.
class ExitDescriptor final {
  public:
    [[nodiscard]] static int open(pid_t child);
};

} // namespace workpane::platform
