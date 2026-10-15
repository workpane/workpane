#pragma once

#include "ui/markdown/Block.h"
#include "ui/markdown/Span.h"

#include <string_view>
#include <vector>

namespace workpane::ui::markdown {

// Reads CommonMark and GitHub extensions through the pinned MD4C parser.
class Parser final {
  public:
    [[nodiscard]] static std::vector<Block> parse(std::string_view source, bool breaks);
    [[nodiscard]] static std::vector<Span> inlines(std::string_view text);
};

} // namespace workpane::ui::markdown
