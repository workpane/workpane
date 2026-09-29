#include "ui/components/containers/Stack.h"

#include <string>

namespace workpane::ui {

Stack::Stack(NodeId id) : Component(id) {}

std::string_view Stack::kind() const {
    return "stack";
}

std::size_t Stack::childLimit() const {
    return unlimitedChildren;
}

Alignment Stack::defaultRowAlignment() const {
    return Alignment::Stretch;
}

void Stack::readProperties(json::ObjectReader& reader) {
    reader.readInteger("current", m_current, 0, 100000, json::Presence::Optional);
}

Component::Restore Stack::keep() {
    return kept(m_current);
}

Result<void> Stack::validateChildren() const {
    if (!children().empty() && static_cast<std::size_t>(m_current) >= children().size()) {
        return Result<void>::failure({"ui_stack_current", "A stack shows one of the pages it holds", std::to_string(m_current)});
    }

    return Result<void>::success();
}

ImVec2 Stack::measureContent(RenderContext& context, float availableWidth) {
    Component* page = current();
    return page == nullptr ? ImVec2(0.0F, 0.0F) : page->measure(context, availableWidth);
}

void Stack::render(RenderContext& context, const ImRect& bounds) {
    if (Component* page = current(); page != nullptr) {
        page->draw(context, bounds);
    }
}

Component* Stack::current() const {
    const auto index = static_cast<std::size_t>(m_current);
    return index < children().size() ? children()[index].get() : nullptr;
}

} // namespace workpane::ui
