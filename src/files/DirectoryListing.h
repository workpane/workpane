#pragma once

#include "Result.h"
#include "files/DirectoryEntry.h"

#include <filesystem>
#include <vector>

namespace workpane::files {

// Lists one folder with the kind of every entry, where a link is reported as a link and never followed.
class DirectoryListing final {
  public:
    [[nodiscard]] static Result<std::vector<DirectoryEntry>> list(const std::filesystem::path& folder);
};

} // namespace workpane::files
