#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace workpane::tests {

class SshTerminalProbeHelper final {
  public:
    static void verify(const std::filesystem::path& executable, const std::vector<std::string>& control, const std::vector<std::string>& transfer, std::vector<std::string> arguments, const std::filesystem::path& root);

  private:
    [[nodiscard]] static std::string command();
};

} // namespace workpane::tests
