#include "ui/components/inputs/TextField.h"

#include "ui/TextFieldOptions.h"
#include "ui/TextFieldResult.h"
#include "ui/Widgets.h"
#include "ui/theme/Theme.h"

#include <algorithm>
#include <utility>

namespace workpane::ui {

TextField::TextField(NodeId id) : Component(id) {}

std::string_view TextField::kind() const {
    return "textField";
}

Result<void> TextField::command(RenderContext& context, std::string_view name, const json::Json& arguments) {
    if (name != "focus") {
        return Component::command(context, name, arguments);
    }

    bool selectAll = false;
    json::ObjectReader reader(arguments, "textField.focus");
    reader.read("selectAll", selectAll, json::Presence::Optional);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return finished;
    }

    m_focus = true;
    m_selectAll = selectAll;

    return Result<void>::success();
}

void TextField::readProperties(json::ObjectReader& reader) {
    std::string value = m_value;
    reader.read("value", value, json::Presence::Optional);

    if (value != m_value) {
        m_value = std::move(value);
        m_reload = true;
    }

    readText(reader, "placeholder", m_placeholder);
    reader.read("password", m_password, json::Presence::Optional).read("readOnly", m_readOnly, json::Presence::Optional).read("clearButton", m_clearButton, json::Presence::Optional).read("monospace", m_monospace, json::Presence::Optional).read("arrows", m_arrows, json::Presence::Optional);
}

Component::Restore TextField::keep() {
    return kept(m_value, m_reload, m_placeholder, m_password, m_readOnly, m_clearButton, m_monospace, m_arrows);
}

ImVec2 TextField::measureContent(RenderContext& context, float availableWidth) {
    return {std::min(availableWidth, context.metric(ThemeMetric::SettingsControlMinimumWidth)), Widgets::controlHeight(context)};
}

void TextField::render(RenderContext& context, const ImRect& bounds) {
    const std::string placeholder = context.text(m_placeholder);
    const TextFieldOptions options{.placeholder = placeholder, .password = m_password, .readOnly = m_readOnly, .clearButton = m_clearButton, .monospace = m_monospace, .reload = m_reload, .focus = m_focus, .selectAll = m_selectAll, .arrows = m_arrows};
    const TextFieldResult result = Widgets::textField(context, "##text", bounds, m_value, options);
    m_reload = false;
    m_focus = false;
    m_selectAll = false;

    if (result.changed) {
        context.emit(id(), "change", {{"value", m_value}}, {{"value", "value"}});
    }

    if (result.submitted) {
        context.emit(id(), "submit", {{"value", m_value}});
    }

    if (result.arrow != ImGuiDir_None) {
        context.emit(id(), "arrow", {{"direction", result.arrow == ImGuiDir_Up ? "up" : "down"}});
    }

    // Leaving the field without Enter still finishes the edit, which is how a setting commits what the reader typed.
    if (result.deactivated && !result.submitted) {
        context.emit(id(), "blur", {{"value", m_value}});
    }
}

} // namespace workpane::ui
