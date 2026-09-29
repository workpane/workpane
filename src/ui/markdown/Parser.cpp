#include "ui/markdown/Parser.h"

#include "ui/markdown/BlockReader.h"
#include "ui/markdown/InlineReader.h"

namespace workpane::ui::markdown {

std::vector<Block> Parser::parse(std::string_view source, bool breaks) {
    return BlockReader(source, breaks).read();
}

std::vector<Span> Parser::inlines(std::string_view text) {
    return InlineReader().read(text);
}

} // namespace workpane::ui::markdown
