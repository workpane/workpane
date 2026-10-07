#pragma once

#include "ui/shell/ModeEntry.h"
#include "ui/theme/FontRole.h"

#include <imgui_internal.h>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace workpane::ui {

class RenderContext;

// The left bar of destinations: primary ones on top, secondary ones at the bottom, and the current one marked by the accent bar.
class ModeBar final {
  public:
    [[nodiscard]] float width(RenderContext& context, const std::vector<ModeEntry>& entries) const;
    [[nodiscard]] std::optional<std::string> draw(RenderContext& context, const ImRect& bounds, const std::vector<ModeEntry>& entries, const std::string& current);

  private:
    static constexpr float fadeInSeconds{0.08F};
    static constexpr float fadeOutSeconds{0.16F};
    static constexpr float hoverOpacity{0.72F};
    static constexpr float selectionMarker{2.0F};
    static constexpr const char* identityScope{"mode-bar"};

    [[nodiscard]] static FontRole labelFont(RenderContext& context, bool selected);
    [[nodiscard]] static float longestWord(RenderContext& context, const std::string& title);

    [[nodiscard]] float buttonHeight(RenderContext& context, const ModeEntry& entry, float width) const;
    [[nodiscard]] bool drawButton(RenderContext& context, const ImRect& bounds, const ModeEntry& entry, bool selected);

    std::map<std::string, float, std::less<>> m_fades;
};

} // namespace workpane::ui
