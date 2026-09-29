#include "files/TextSearch.h"

#include "files/TreeWalk.h"
#include "text/Utf8.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace workpane::files {

// A file is searched only when it holds text: no larger than the bound, without a NUL byte and spelled as UTF-8.
Result<TextSearch::Found> TextSearch::search(const std::filesystem::path& root, std::string_view text, std::size_t maximumMatches, std::uintmax_t maximumFileBytes, const std::vector<std::string>& skipped, const std::atomic<bool>& stop) {
    Found found{{}, true};
    const std::string wanted = folded(text);

    // A text that spans lines is held by no line.
    if (wanted.find_first_of("\r\n") != std::string::npos) {
        return Result<Found>::success(std::move(found));
    }

    // clang-format off
    const TreeWalk::Visitor visitor = [&](const std::filesystem::path& file, const std::string& relative) {
        std::error_code error;
        const std::uintmax_t size = std::filesystem::file_size(file, error);

        if (error || size == 0 || size > maximumFileBytes) {
            return true;
        }

        std::string content(static_cast<std::size_t>(size), '\0');
        std::ifstream stream(file, std::ios::binary);
        stream.read(content.data(), static_cast<std::streamsize>(content.size()));
        content.resize(static_cast<std::size_t>(stream.gcount()));

        if (content.find('\0') != std::string::npos || !text::Utf8::valid(content)) {
            return true;
        }

        // The file is folded once and searched as a whole, and the lines are counted only up to each match, so a large file costs one pass.
        const std::string haystack = folded(content);
        std::size_t line = 1;
        std::size_t lineStart = 0;
        std::size_t counted = 0;

        for (std::size_t at = haystack.find(wanted); at != std::string::npos; at = lineStart > content.size() ? std::string::npos : haystack.find(wanted, lineStart)) {
            for (; counted < at; ++counted) {
                if (content[counted] == '\n') {
                    ++line;
                    lineStart = counted + 1;
                }
            }

            if (found.matches.size() == maximumMatches) {
                found.complete = false;
                return false;
            }

            const std::size_t lineEnd = std::min(content.find('\n', at), content.size());
            found.matches.push_back({relative, line, text::Utf8::truncated(trimmed(std::string_view(content).substr(lineStart, lineEnd - lineStart)), matchCharacters)});

            // The next match is looked for on the next line, since a line is answered once.
            counted = lineEnd + 1;
            lineStart = lineEnd + 1;
            ++line;
        }

        return true;
    };
    // clang-format on

    if (const auto visited = TreeWalk::visit(root, skipped, visitor, stop); !visited.hasValue()) {
        return Result<Found>::failure(visited.error());
    }

    found.complete = found.complete && !stop.load();

    return Result<Found>::success(std::move(found));
}

std::string TextSearch::folded(std::string_view text) {
    std::string lowered(text);

    for (char& character : lowered) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }

    return lowered;
}

std::string TextSearch::trimmed(std::string_view line) {
    const std::size_t first = line.find_first_not_of(" \t\r");

    if (first == std::string_view::npos) {
        return {};
    }

    return std::string(line.substr(first, line.find_last_not_of(" \t\r") - first + 1));
}

} // namespace workpane::files
