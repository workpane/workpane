#pragma once

#include "ui/markdown/Block.h"
#include "ui/markdown/Span.h"

#include <string_view>
#include <vector>

namespace workpane::ui::markdown {

// Reads the part of CommonMark a plugin writes: headings, paragraphs, emphasis, lists, quotes, links, code and rules.
class Parser final {
  public:
    [[nodiscard]] static std::vector<Block> parse(std::string_view source, bool breaks);
    [[nodiscard]] static std::vector<Span> inlines(std::string_view text);
};

} // namespace workpane::ui::markdown
