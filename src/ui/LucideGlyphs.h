#pragma once

#include <imgui.h>

#include <optional>
#include <string_view>
#include <vector>

namespace workpane::ui {

// Every glyph of the bundled Lucide face under its Lucide name, which is how a plugin names any icon the face draws.
class LucideGlyphs final {
  public:
    [[nodiscard]] static std::optional<ImWchar> find(std::string_view name);

  private:
    struct Entry final {
        std::string_view name;
        ImWchar glyph;
    };

    [[nodiscard]] static const std::vector<Entry>& table();
};

} // namespace workpane::ui
