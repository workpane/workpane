#include "ui/markdown/Span.h"

namespace workpane::ui::markdown {

bool Span::sameStyle(const Span& other) const {
    return strong == other.strong && emphasis == other.emphasis && code == other.code && link == other.link;
}

} // namespace workpane::ui::markdown
