#include "ui/components/text/PageHeader.h"

#include "ui/Widgets.h"
#include "ui/components/containers/Axis.h"
#include "ui/theme/Theme.h"

namespace workpane::ui {

PageHeader::PageHeader(NodeId id) : LinearContainer(id, Axis::Horizontal) {
    m_spacing = pageHeaderSpacing;
    m_justify = Justify::End;
}

std::string_view PageHeader::kind() const {
    return "pageHeader";
}

void PageHeader::readProperties(json::ObjectReader& reader) {
    readText(reader, "title", m_title);
    readText(reader, "caption", m_caption);
}

Component::Restore PageHeader::keep() {
    return kept(m_title, m_caption);
}

ImVec2 PageHeader::measureContent(RenderContext& context, float availableWidth) {
    return {availableWidth, context.metric(ThemeMetric::PageHeaderHeight)};
}

void PageHeader::paintSurface(RenderContext& context, const ImRect& bounds) {
    Widgets::pageHeader(context, bounds, context.text(m_title), context.text(m_caption));
}

// The actions of the page are laid out after the title and the caption, and the divider keeps its own pixel below them.
Insets PageHeader::contentInsets(RenderContext& context) const {
    return {0.0F, pageHeaderTrailing * context.scale(), 1.0F, Widgets::pageHeaderLeading(context, context.text(m_title), context.text(m_caption))};
}

} // namespace workpane::ui
