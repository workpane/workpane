#include "ui/components/views/DarkModern.h"

namespace workpane::ui {

std::vector<std::pair<TextEditor::Color, Color>> DarkModern::colors() {
    std::vector<std::pair<TextEditor::Color, Color>> painted;

    for (const auto& [role, rgb] : roles) {
        painted.emplace_back(role, color(rgb));
    }

    return painted;
}

Color DarkModern::currentLine() {
    return color(currentLineColor);
}

Color DarkModern::occurrence() {
    return color(occurrenceColor);
}

Color DarkModern::color(std::uint32_t rgb) {
    return Color::rgb(static_cast<int>((rgb >> 16U) & 0xFFU), static_cast<int>((rgb >> 8U) & 0xFFU), static_cast<int>(rgb & 0xFFU));
}

} // namespace workpane::ui
