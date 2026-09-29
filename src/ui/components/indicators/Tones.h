#pragma once

#include "json/ObjectReader.h"
#include "ui/theme/Theme.h"

#include <string_view>

namespace workpane::ui {

enum class Tone { Neutral, Accent, Success, Warning, Danger, Information };

// A tone answers the roles of its family: the fill, the ink written over the fill, a subtle background and the text color readable on it.
class Tones final {
  public:
    static void read(json::ObjectReader& reader, std::string_view key, Tone& out);
    [[nodiscard]] static ThemeColor color(Tone tone);
    [[nodiscard]] static ThemeColor ink(Tone tone);
    [[nodiscard]] static ThemeColor background(Tone tone);
    [[nodiscard]] static ThemeColor text(Tone tone);
};

} // namespace workpane::ui
