#pragma once

#include "ui/Color.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"

namespace workpane::ui {

class AccentTheme : public Theme {
  public:
    [[nodiscard]] Color color(ThemeColor role) const override;
    [[nodiscard]] float metric(ThemeMetric role) const override;
    [[nodiscard]] FontRole font(ThemeFont role) const override;

  protected:
    [[nodiscard]] virtual Color accent() const = 0;
    [[nodiscard]] virtual Color success() const = 0;

  private:
    static constexpr int hoverDarkening{115};
    static constexpr int pressedDarkening{140};
    static constexpr float backgroundMix{0.12F};
    static constexpr float textMix{0.3F};
    static constexpr float selectionOpacity{0.45F};
    static constexpr float highlightOpacity{0.35F};
    static constexpr float overlayOpacity{0.45F};

    [[nodiscard]] Color background(ThemeColor tone) const;
    [[nodiscard]] Color readable(ThemeColor tone) const;
};

} // namespace workpane::ui
