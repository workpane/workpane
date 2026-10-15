#include "platform/ProcessDirectory.h"

#include <libproc.h>
#include <sys/proc_info.h>

#include <string>

namespace workpane::platform {

std::string ProcessDirectory::of(long processId) {
    proc_vnodepathinfo information{};
    const int size = ::proc_pidinfo(static_cast<int>(processId), PROC_PIDVNODEPATHINFO, 0, &information, sizeof(information));

    if (size != static_cast<int>(sizeof(information))) {
        return {};
    }

    return std::string(information.pvi_cdir.vip_path);
}

} // namespace workpane::platform
