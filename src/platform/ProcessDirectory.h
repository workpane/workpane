#pragma once

#include <string>

namespace workpane::platform {

// Answers the directory a process stands in, which a platform either tells anybody who asks or tells nobody.
class ProcessDirectory final {
  public:
    [[nodiscard]] static std::string of(long processId);
};

} // namespace workpane::platform
