#pragma once

#include <string_view>

namespace workpane::ui {

// The path of an asset a plugin names, which stays inside the assets folder of that plugin whatever it spells.
class AssetPath final {
  public:
    [[nodiscard]] static bool safe(std::string_view path);
};

} // namespace workpane::ui
