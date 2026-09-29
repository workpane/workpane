#pragma once

#include "ui/markdown/Span.h"

#include <string>
#include <vector>

namespace workpane::ui::markdown {

enum class BlockKind { Heading, Paragraph, Bullet, Numbered, Quote, Code, Rule };

struct Block final {
    BlockKind kind{BlockKind::Paragraph};
    int level{0};
    int ordinal{0};
    std::vector<Span> spans;
    std::string code;
    std::string language;
};

} // namespace workpane::ui::markdown
