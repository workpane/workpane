#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace workpane::tests {

class SshScopeProbeHelper {
  public:
    static void verify(const std::filesystem::path& executable, std::vector<std::string> arguments, const std::filesystem::path& workspace);
};

} // namespace workpane::tests
