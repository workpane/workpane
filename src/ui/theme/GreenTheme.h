#pragma once

#include "ui/Color.h"
#include "ui/theme/AccentTheme.h"

#include <string_view>

namespace workpane::ui {

class GreenTheme final : public AccentTheme {
  public:
    [[nodiscard]] std::string_view id() const override;
    [[nodiscard]] std::string_view titleKey() const override;

  protected:
    [[nodiscard]] Color accent() const override;
    [[nodiscard]] Color success() const override;
};

} // namespace workpane::ui
