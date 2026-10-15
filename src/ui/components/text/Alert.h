#pragma once

#include "json/ObjectReader.h"
#include "ui/components/indicators/Tones.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/model/TextValue.h"

#include <imgui_internal.h>

#include <string_view>

namespace workpane::ui {

// An alert states something the reader must see on the background of its tone with one rule of that tone down its left edge, and one that appears scrolls itself into view.
// Danger is the tone of an alert that names none, since most alerts report a problem.
class Alert final : public Component {
  public:
    explicit Alert(NodeId id);
    [[nodiscard]] std::string_view kind() const override;

  protected:
    [[nodiscard]] bool revealing() const override;
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    static constexpr float ruleWidth{2.0F};

    [[nodiscard]] static ImVec2 inset(const RenderContext& context);

    TextValue m_text;
    Tone m_tone{Tone::Danger};
    bool m_mounted{false};
    bool m_reveal{false};
};

} // namespace workpane::ui
