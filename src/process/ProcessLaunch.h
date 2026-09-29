#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace workpane::process {

// What a program is started with: the executable, its arguments without any shell, its directory and the variables added to or cleared from the environment it inherits.
// A program given an input reads that text and then the end of its input, so an empty one ends its input from the start, and one given none keeps its input open for writes.
struct ProcessLaunch final {
    std::filesystem::path program;
    std::vector<std::string> arguments;
    std::filesystem::path directory;
    std::vector<std::pair<std::string, std::string>> variables;
    std::vector<std::string> cleared;
    std::optional<std::string> input{};
};

} // namespace workpane::process
