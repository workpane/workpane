#pragma once

#include "Result.h"
#include "ui/theme/Theme.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace workpane::ui {

// Lua names a color role by the same word everywhere, so a component never carries a literal color.
class ThemeColorNames final {
  public:
    [[nodiscard]] static const std::vector<std::pair<ThemeColor, std::string_view>>& all();
    [[nodiscard]] static std::optional<ThemeColor> parse(std::string_view name);
    [[nodiscard]] static Result<std::optional<ThemeColor>> parseOptional(const std::string& name);
};

} // namespace workpane::ui
