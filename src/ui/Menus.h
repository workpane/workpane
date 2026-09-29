#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/MenuItem.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::ui {

class RenderContext;

// The flat menus of the product, opened below a menu button or under the pointer, which answer the item the reader picked.
class Menus final {
  public:
    [[nodiscard]] static Result<std::vector<MenuItem>> parse(const json::Json& items, std::string_view context);
    [[nodiscard]] static std::optional<std::string> popup(RenderContext& context, const char* id, const std::vector<MenuItem>& items);

  private:
    static constexpr float menuInset{4.0F};
    static constexpr float menuMinimumWidth{180.0F};
    static constexpr float menuShortcutSpacing{24.0F};
    static constexpr float menuSeparatorMargin{4.0F};

    [[nodiscard]] static std::optional<std::string> drawItems(RenderContext& context, const std::vector<MenuItem>& items);
};

} // namespace workpane::ui
