#pragma once

#include "Result.h"
#include "app/ApplicationIdentity.h"
#include "app/ApplicationPaths.h"

#include <filesystem>
#include <optional>
#include <string_view>

namespace workpane::app {

// Resolves where the running layout keeps its resources and where the platform keeps the data of the reader.
class ApplicationPathsResolver final {
  public:
    [[nodiscard]] static Result<ApplicationPaths> resolve(const ApplicationIdentity& identity, const std::optional<std::filesystem::path>& dataOverride);
    [[nodiscard]] static std::filesystem::path resourcesFor(const std::filesystem::path& executable, std::string_view directory);

  private:
    [[nodiscard]] static Result<std::filesystem::path> executable();
    [[nodiscard]] static Result<std::filesystem::path> platformData(std::string_view directory);
};

} // namespace workpane::app
