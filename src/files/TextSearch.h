#pragma once

#include "Result.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::files {

// Searches the UTF-8 text files under a folder for a text line by line without regard to the case of ASCII letters, up to a bound on the lines it answers.
class TextSearch final {
  public:
    struct Match final {
        std::string path;
        std::size_t line;
        std::string text;
    };

    struct Found final {
        std::vector<Match> matches;
        bool complete;
    };

    [[nodiscard]] static Result<Found> search(const std::filesystem::path& root, std::string_view text, std::size_t maximumMatches, std::uintmax_t maximumFileBytes, const std::vector<std::string>& skipped, const std::atomic<bool>& stop);

  private:
    static constexpr std::size_t matchCharacters{400};

    [[nodiscard]] static std::string folded(std::string_view text);
    [[nodiscard]] static std::string trimmed(std::string_view line);
};

} // namespace workpane::files
