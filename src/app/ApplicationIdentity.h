#pragma once

#include "Result.h"

#include <string>
#include <string_view>

namespace workpane::app {

struct ApplicationIdentity final {
    std::string name;
    std::string version;
    std::string buildNumber;
    std::string organization;
    std::string identifier;
    std::string resourceDirectory;
    std::string dataDirectory;

    [[nodiscard]] Result<void> validate() const;

  private:
    [[nodiscard]] static bool validText(std::string_view value);
    [[nodiscard]] static bool validSegment(std::string_view value);
};

} // namespace workpane::app
