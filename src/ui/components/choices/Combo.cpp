#include "ui/components/choices/Combo.h"

#include "localization/Localization.h"
#include "ui/TextCase.h"
#include "ui/Widgets.h"
#include "ui/components/choices/ChoiceOptions.h"
#include "ui/theme/Theme.h"

#include <algorithm>
#include <numeric>
#include <utility>

namespace workpane::ui {

Combo::Combo(NodeId id) : Component(id) {}

std::string_view Combo::kind() const {
    return "combo";
}

FocusMode Combo::focusMode() const {
    return FocusMode::Choosing;
}

void Combo::readProperties(json::ObjectReader& reader) {
    reader.read("value", m_value, json::Presence::Optional);
    readText(reader, "placeholder", m_placeholder);
    reader.read("sorted", m_sorted, json::Presence::Optional);
    m_orderGeneration = 0;

    if (!reader.contains("options")) {
        return;
    }

    const json::Json* options = &json::ObjectReader::absent();
    reader.readArray("options", options);
    auto parsed = ChoiceOptions::parse(*options, kind());

    if (!parsed.hasValue()) {
        fail(parsed.error());
        return;
    }

    m_options = std::move(parsed.value());
}

Component::Restore Combo::keep() {
    return kept(m_value, m_placeholder, m_sorted, m_orderGeneration, m_options);
}

Result<void> Combo::validate() const {
    // clang-format off
    const bool known = m_value.empty() || std::ranges::any_of(m_options, [this](const ChoiceOption& option) { return option.value == m_value; });
    // clang-format on

    if (!known) {
        return Result<void>::failure({"ui_option_unknown", "A choice selects a value none of its options carries", m_value});
    }

    return Result<void>::success();
}

ImVec2 Combo::measureContent(RenderContext& context, float availableWidth) {
    return {std::min(availableWidth, context.metric(ThemeMetric::SettingsControlMinimumWidth)), Widgets::controlHeight(context)};
}

void Combo::render(RenderContext& context, const ImRect& bounds) {
    const auto& indices = order(context);
    std::vector<std::string> labels;
    labels.reserve(indices.size());
    int selected = -1;

    for (std::size_t position = 0; position < indices.size(); ++position) {
        const ChoiceOption& option = m_options[indices[position]];
        labels.push_back(context.text(option.text));

        if (option.value == m_value) {
            selected = static_cast<int>(position);
        }
    }

    if (Widgets::combo(context, "##combo", bounds, labels, selected, context.text(m_placeholder))) {
        m_value = m_options[indices[static_cast<std::size_t>(selected)]].value;
        context.emit(id(), "change", {{"value", m_value}}, {{"value", "value"}});
    }
}

// The presented order follows the translated labels, so it is computed again whenever the language or the options change.
const std::vector<std::size_t>& Combo::order(RenderContext& context) {
    if (m_orderGeneration == context.localization().generation() && m_order.size() == m_options.size()) {
        return m_order;
    }

    m_order.resize(m_options.size());
    std::iota(m_order.begin(), m_order.end(), std::size_t{0});

    if (m_sorted) {
        // clang-format off
        std::ranges::stable_sort(m_order, [this, &context](std::size_t left, std::size_t right) { return TextCase::alphabeticalLess(context.text(m_options[left].text), context.text(m_options[right].text)); });
        // clang-format on
    }

    m_orderGeneration = context.localization().generation();

    return m_order;
}

} // namespace workpane::ui
