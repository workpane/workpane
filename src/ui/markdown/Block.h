#pragma once

#include "ui/markdown/Cell.h"
#include "ui/markdown/Span.h"

#include <optional>
#include <string>
#include <vector>

namespace workpane::ui::markdown {

enum class BlockKind { Heading, Paragraph, Bullet, Numbered, Quote, Code, Rule, Table };

struct Block final {
    BlockKind kind{BlockKind::Paragraph};
    int level{0};
    int ordinal{0};
    std::vector<Span> spans;
    std::string code;
    std::string language;
    std::vector<std::vector<Cell>> rows;
    std::optional<bool> checked;
    int quoteDepth{0};
    int listDepth{0};
};

} // namespace workpane::ui::markdown
