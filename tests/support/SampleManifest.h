#pragma once

#include "Result.h"
#include "localization/Localization.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace workpane::tests {

// The manifest of a plugin with one navigation item and its catalog, which the registry tests start from and then break on purpose.
class SampleManifest final {
  public:
    [[nodiscard]] static nlohmann::json create(const std::string& id, const std::filesystem::path& plugins);
    [[nodiscard]] static Result<void> install(localization::Localization& localization, const std::string& id);
};

} // namespace workpane::tests
