#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <vector>

namespace workpane::tests {

class SshWorkspaceProbeHelper final {
  public:
    static void verify(const std::filesystem::path& executable, const nlohmann::json& profile, const std::vector<int>& ports, const std::filesystem::path& workspace);
};

} // namespace workpane::tests
