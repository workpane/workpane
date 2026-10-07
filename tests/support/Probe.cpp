#include "support/Probe.h"

namespace workpane::tests {

Probe::Probe(ui::NodeId id) : ui::Component(id) {}

std::string_view Probe::kind() const {
    return "probe";
}

std::map<ui::NodeId, ImRect>& Probe::placed() {
    static std::map<ui::NodeId, ImRect> rectangles;
    return rectangles;
}

void Probe::readProperties(json::ObjectReader& reader) {
    double contentWidth = m_contentWidth;
    double contentHeight = m_contentHeight;
    reader.readNumber("contentWidth", contentWidth, 0.0, 100000.0, json::Presence::Optional).readNumber("contentHeight", contentHeight, 0.0, 100000.0, json::Presence::Optional);
    m_contentWidth = static_cast<float>(contentWidth);
    m_contentHeight = static_cast<float>(contentHeight);
}

ui::Component::Restore Probe::keep() {
    return kept(m_contentWidth, m_contentHeight);
}

ImVec2 Probe::measureContent(ui::RenderContext&, float) {
    return {m_contentWidth, m_contentHeight};
}

void Probe::render(ui::RenderContext&, const ImRect& bounds) {
    placed()[id()] = bounds;
}

} // namespace workpane::tests
