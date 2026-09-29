#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace workpane::tests {

// The node declarations the interface suites mount, including one realistic node of every kind a plugin can build.
class ComponentSpecs final {
  public:
    [[nodiscard]] static nlohmann::json node(std::uint64_t id, std::string kind, nlohmann::json props = nlohmann::json::object(), std::vector<nlohmann::json> children = {});
    [[nodiscard]] static nlohmann::json label(std::uint64_t id);
    [[nodiscard]] static std::map<std::string, nlohmann::json> samples();
};

} // namespace workpane::tests
