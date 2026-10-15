#include "ui/components/indicators/BusyIndicator.h"

#include "ui/Painter.h"
#include "ui/components/indicators/IndicatorSizes.h"
#include "ui/theme/Theme.h"

#include <algorithm>

namespace workpane::ui {

BusyIndicator::BusyIndicator(NodeId id) : Component(id) {}

std::string_view BusyIndicator::kind() const {
    return "busyIndicator";
}

Alignment BusyIndicator::defaultColumnAlignment() const {
    return Alignment::Center;
}

void BusyIndicator::readProperties(json::ObjectReader& reader) {
    IndicatorSizes::read(reader, "size", m_size);
    reader.read("running", m_running, json::Presence::Optional);
}

Component::Restore BusyIndicator::keep() {
    return kept(m_size, m_running);
}

ImVec2 BusyIndicator::measureContent(RenderContext& context, float) {
    const float size = m_size * context.scale();
    return {size, size};
}

// A stopped indicator draws nothing, so whoever places one decides when it turns.
void BusyIndicator::render(RenderContext& context, const ImRect& bounds) {
    if (!m_running) {
        return;
    }

    const float radius = std::min(bounds.GetWidth(), bounds.GetHeight()) / 2.0F - 2.0F * context.scale();
    Painter::busyRing(*ImGui::GetWindowDrawList(), bounds.GetCenter(), radius, context.time(), context.color(ThemeColor::Accent));
    context.requestFrame();
}

} // namespace workpane::ui
