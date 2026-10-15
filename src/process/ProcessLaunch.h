#pragma once

#include "Result.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace workpane::process {

// What a program is started with: the executable, its arguments without any shell, its directory and the variables added to or cleared from the environment it inherits.
// A program given an input reads those bytes and then the end of its input, so an empty one ends its input from the start, and one given none keeps its input open for writes.
struct ProcessLaunch final {
    static constexpr std::size_t maximumInputBytes{64U << 20U};

    std::filesystem::path program;
    std::vector<std::string> arguments;
    std::filesystem::path directory;
    std::vector<std::pair<std::string, std::string>> variables;
    std::vector<std::string> cleared;
    std::optional<std::string> input{};

    [[nodiscard]] Result<void> validate() const;

  private:
    static constexpr std::size_t maximumItems{256};
    static constexpr std::size_t maximumText{65536};
    static constexpr std::size_t maximumPath{4096};

    [[nodiscard]] static bool text(std::string_view value, std::size_t maximum);
    [[nodiscard]] static bool variable(std::string_view value);
};

} // namespace workpane::process
