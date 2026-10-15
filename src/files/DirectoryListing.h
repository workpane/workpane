#pragma once

#include "Result.h"
#include "files/DirectoryEntry.h"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <vector>

namespace workpane::files {

// Lists one folder with the kind of every entry, where a link is reported as a link and never followed.
class DirectoryListing final {
  public:
    struct Listed final {
        std::vector<DirectoryEntry> entries;
        bool complete;
    };

    [[nodiscard]] static Result<Listed> list(const std::filesystem::path& folder, std::size_t maximum, const std::atomic<bool>& stop);
};

} // namespace workpane::files
