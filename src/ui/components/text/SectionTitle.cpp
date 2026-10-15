#include "ui/components/text/SectionTitle.h"

#include "ui/TextCaseHelper.h"
#include "ui/WidgetHelper.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"

#include <cmath>

namespace workpane::ui {

SectionTitle::SectionTitle(NodeId id) : Component(id) {}

std::string_view SectionTitle::kind() const {
    return "sectionTitle";
}

void SectionTitle::readProperties(json::ObjectReader& reader) {
    readText(reader, "text", m_text);
}

Component::Restore SectionTitle::keep() {
    return kept(m_text);
}

ImVec2 SectionTitle::measureContent(RenderContext& context, float) {
    const FontRole font = context.font(ThemeFont::SectionTitle);
    return {std::ceil(WidgetHelper::textSize(context, font, TextCaseHelper::upper(context.text(m_text))).x), WidgetHelper::lineHeight(context, font)};
}

void SectionTitle::render(RenderContext& context, const ImRect& bounds) {
    WidgetHelper::sectionTitle(context, *ImGui::GetWindowDrawList(), bounds, context.text(m_text));
}

} // namespace workpane::ui
