#pragma once

#include "Result.h"
#include "ui/Color.h"
#include "ui/Fonts.h"
#include "ui/Icon.h"

#include <imgui.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::ui {

// The names that reach icons: the names of the product first, then the Lucide name of every glyph of the bundled face, so a plugin draws any icon the face holds.
class IconCatalog final {
  public:
    [[nodiscard]] static std::vector<std::string_view> productNames();
    [[nodiscard]] static std::optional<Icon> parse(std::string_view name);
    [[nodiscard]] static Result<std::optional<Icon>> parseOptional(const std::string& name);
    static void draw(const Fonts& fonts, ImDrawList& list, Icon icon, ImVec2 topLeft, float size, Color color);

  private:
    struct Entry final {
        Icon icon;
        std::string_view name;
    };

    [[nodiscard]] static const std::vector<Entry>& table();
};

} // namespace workpane::ui
