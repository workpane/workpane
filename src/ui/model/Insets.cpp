#include "ui/model/Insets.h"

#include <algorithm>

namespace workpane::ui {

float Insets::horizontal() const {
    return left + right;
}

float Insets::vertical() const {
    return top + bottom;
}

ImRect Insets::shrink(const ImRect& bounds) const {
    return {ImVec2(bounds.Min.x + left, bounds.Min.y + top), ImVec2(std::max(bounds.Min.x + left, bounds.Max.x - right), std::max(bounds.Min.y + top, bounds.Max.y - bottom))};
}

} // namespace workpane::ui
