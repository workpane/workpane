#include "ui/components/indicators/StatusIndicator.h"

#include "ui/Painter.h"
#include "ui/components/indicators/IndicatorSizes.h"
#include "ui/components/indicators/Tones.h"

namespace workpane::ui {

StatusIndicator::StatusIndicator(NodeId id) : Component(id) {}

std::string_view StatusIndicator::kind() const {
    return "statusIndicator";
}

Alignment StatusIndicator::defaultColumnAlignment() const {
    return Alignment::Start;
}

void StatusIndicator::readProperties(json::ObjectReader& reader) {
    Tones::read(reader, "tone", m_tone);
    IndicatorSizes::read(reader, "size", m_size);
}

Component::Restore StatusIndicator::keep() {
    return kept(m_tone, m_size);
}

ImVec2 StatusIndicator::measureContent(RenderContext& context, float) {
    const float size = m_size * context.scale();
    return {size, size};
}

void StatusIndicator::render(RenderContext& context, const ImRect& bounds) {
    Painter::indicator(*ImGui::GetWindowDrawList(), bounds.GetCenter(), m_size * context.scale(), context.color(Tones::color(m_tone)));
}

} // namespace workpane::ui
