#include "ui/markdown/Parser.h"

#include "ui/markdown/BlockReader.h"

#include <utility>

namespace workpane::ui::markdown {

std::vector<Block> Parser::parse(std::string_view source, bool breaks) {
    return BlockReader(source, breaks).read();
}

std::vector<Span> Parser::inlines(std::string_view text) {
    auto blocks = BlockReader(text, true).read();

    return blocks.empty() ? std::vector<Span>{} : std::move(blocks.front().spans);
}

} // namespace workpane::ui::markdown
