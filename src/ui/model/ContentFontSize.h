#pragma once

#include <optional>

namespace workpane::ui {

enum class FontStep { Increase, Decrease, Reset };

// Every surface that is read owns its own size inside one shared range and answers the zoom keys only while it has the keyboard.
class ContentFontSize final {
  public:
    static constexpr double minimum{8.0};
    static constexpr double maximum{36.0};
    static constexpr double standard{11.0};

    [[nodiscard]] static std::optional<FontStep> shortcutStep();
    [[nodiscard]] static double stepped(double size, FontStep step);
};

} // namespace workpane::ui
