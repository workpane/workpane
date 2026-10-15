#include "ui/markdown/Span.h"

namespace workpane::ui::markdown {

bool Span::sameStyle(const Span& other) const {
    return image.empty() && other.image.empty() && strong == other.strong && emphasis == other.emphasis && code == other.code && link == other.link && strike == other.strike;
}

} // namespace workpane::ui::markdown
