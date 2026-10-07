#include "ui/components/containers/Spacer.h"

namespace workpane::ui {

Spacer::Spacer(NodeId id) : Component(id) {}

std::string_view Spacer::kind() const {
    return "spacer";
}

void Spacer::readProperties(json::ObjectReader&) {}

Component::Restore Spacer::keep() {
    return kept();
}

ImVec2 Spacer::measureContent(RenderContext&, float) {
    return {0.0F, 0.0F};
}

void Spacer::render(RenderContext&, const ImRect&) {}

} // namespace workpane::ui
