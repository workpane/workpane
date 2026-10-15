#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace workpane::tests {

class SftpProbeHelper final {
  public:
    static void verify(const std::filesystem::path& executable, std::vector<std::string> arguments, const std::filesystem::path& root);
};

} // namespace workpane::tests
