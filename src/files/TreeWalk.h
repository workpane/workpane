#pragma once

#include "Result.h"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace workpane::files {

// Walks the regular files under a folder without following linked folders and without entering the folders it is told to leave out, until its stop is set.
class TreeWalk final {
  public:
    static constexpr std::size_t maximumEntries{100000};

    struct Walked final {
        std::vector<std::string> paths;
        bool complete;
    };

    using Visitor = std::function<bool(const std::filesystem::path& file, const std::string& relative)>;

    [[nodiscard]] static Result<bool> visit(const std::filesystem::path& root, const std::vector<std::string>& skipped, const Visitor& visitor, const std::atomic<bool>& stop, std::size_t maximumInspected);
    [[nodiscard]] static Result<Walked> walk(const std::filesystem::path& root, std::size_t maximum, const std::vector<std::string>& skipped, const std::atomic<bool>& stop);
};

} // namespace workpane::files
