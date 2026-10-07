#pragma once

#include "ui/markdown/Span.h"
#include "ui/theme/Style.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::ui::markdown {

// Reads the spans of one paragraph: emphasis, strong text, code, links and autolinks.
class InlineReader final {
  public:
    [[nodiscard]] std::vector<Span> read(std::string_view text);

  private:
    static constexpr std::size_t strongestRun{3};
    static constexpr std::size_t maximumNesting{32};
    static constexpr std::size_t longestLabel{1000};
    static constexpr std::size_t longestDestination{4096};

    struct Style final {
        bool strong{false};
        bool emphasis{false};
        std::string link;
    };

    struct LinkParts final {
        std::string_view label;
        std::string_view destination;
        std::size_t end{0};
    };

    void parse(std::string_view text, const Style& style, std::size_t depth);
    void push(std::string_view text, const Style& style, bool code);
    [[nodiscard]] static std::string codeContent(std::string_view text);
    [[nodiscard]] static std::size_t skipCode(std::string_view text, std::size_t position);
    [[nodiscard]] static std::size_t closingTicks(std::string_view text, std::size_t from, std::size_t length);
    [[nodiscard]] static bool opensEmphasis(std::string_view text, std::size_t position, std::size_t length);
    [[nodiscard]] static std::size_t closingEmphasis(std::string_view text, std::size_t from, char mark, std::size_t length);
    [[nodiscard]] static std::optional<LinkParts> link(std::string_view text, std::size_t open);
    [[nodiscard]] static std::optional<LinkParts> autolink(std::string_view text, std::size_t open);

    std::vector<Span> m_spans;
};

} // namespace workpane::ui::markdown
