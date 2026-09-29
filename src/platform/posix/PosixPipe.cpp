#include "platform/posix/PosixPipe.h"

#include <fcntl.h>
#include <unistd.h>

namespace workpane::platform {

// Linux opens both ends closed on exec at once, while macOS has no such call, so its ends are marked before anything else runs and every program the product starts closes what it did not ask for.
bool PosixPipe::open(std::array<int, 2>& ends) {
#if defined(__linux__)
    return ::pipe2(ends.data(), O_CLOEXEC) == 0;
#else
    if (::pipe(ends.data()) != 0) {
        return false;
    }

    for (const int end : ends) {
        ::fcntl(end, F_SETFD, FD_CLOEXEC);
    }

    return true;
#endif
}

} // namespace workpane::platform
