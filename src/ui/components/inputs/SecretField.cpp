#include "ui/components/inputs/SecretField.h"

#include "ui/IconCatalog.h"
#include "ui/TextFieldOptions.h"
#include "ui/TextFieldResult.h"
#include "ui/Widgets.h"
#include "ui/theme/Theme.h"

#include <algorithm>
#include <utility>

namespace workpane::ui {

SecretField::SecretField(NodeId id) : Component(id) {}

std::string_view SecretField::kind() const {
    return "secretField";
}

void SecretField::readProperties(json::ObjectReader& reader) {
    std::string value = m_value;
    reader.read("value", value, json::Presence::Optional);

    if (value != m_value) {
        m_value = std::move(value);
        m_reload = true;
    }

    readText(reader, "placeholder", m_placeholder);
    reader.read("revealed", m_revealed, json::Presence::Optional).read("confirmReveal", m_confirmReveal, json::Presence::Optional);
}

Component::Restore SecretField::keep() {
    return kept(m_value, m_reload, m_placeholder, m_revealed, m_confirmReveal);
}

ImVec2 SecretField::measureContent(RenderContext& context, float availableWidth) {
    return {std::min(availableWidth, context.metric(ThemeMetric::SettingsControlMinimumWidth)), Widgets::controlHeight(context)};
}

void SecretField::render(RenderContext& context, const ImRect& bounds) {
    const float button = Widgets::controlHeight(context);
    const float spacing = revealSpacing * context.scale();
    const ImRect field(bounds.Min, ImVec2(bounds.Max.x - button - spacing, bounds.Max.y));
    const ImRect reveal(ImVec2(bounds.Max.x - button, bounds.Min.y), bounds.Max);
    const std::string placeholder = context.text(m_placeholder);
    const TextFieldOptions options{.placeholder = placeholder, .password = !m_revealed, .reload = m_reload};
    const TextFieldResult result = Widgets::textField(context, "##secret", field, m_value, options);
    m_reload = false;

    if (result.changed) {
        context.emit(id(), "change", {{"value", m_value}}, {{"value", "value"}});
    }

    if (result.submitted) {
        context.emit(id(), "submit", {{"value", m_value}});
    }

    // Leaving the field without Enter still finishes the edit, which is how a setting commits a key the reader typed.
    if (result.deactivated && !result.submitted) {
        context.emit(id(), "blur", {{"value", m_value}});
    }

    // Hiding is immediate, while revealing either happens at once or asks the owner, which answers by setting the revealed property.
    if (Widgets::button(context, "##reveal", reveal, {}, m_revealed ? Icon::Hidden : Icon::Visible, ButtonVariant::Icon)) {
        if (m_revealed) {
            m_revealed = false;
            context.emit(id(), "reveal", {{"revealed", false}}, {{"revealed", "revealed"}});
        } else if (m_confirmReveal) {
            context.emit(id(), "reveal-request");
        } else {
            m_revealed = true;
            context.emit(id(), "reveal", {{"revealed", true}}, {{"revealed", "revealed"}});
        }
    }
}

} // namespace workpane::ui
