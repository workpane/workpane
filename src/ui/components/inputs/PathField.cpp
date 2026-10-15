#include "ui/components/inputs/PathField.h"

#include "ui/Icon.h"
#include "ui/Painter.h"
#include "ui/TextFieldOptions.h"
#include "ui/TextFieldResult.h"
#include "ui/WidgetHelper.h"
#include "ui/theme/Theme.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace workpane::ui {

PathField::PathField(NodeId id) : Component(id) {}

std::string_view PathField::kind() const {
    return "pathField";
}

Result<void> PathField::command(RenderContext& context, std::string_view name, const json::Json& arguments) {
    if (name != "focus") {
        return Component::command(context, name, arguments);
    }

    bool selectAll = false;
    json::ObjectReader reader(arguments, "pathField.focus");
    reader.read("selectAll", selectAll, json::Presence::Optional);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return finished;
    }

    m_focus = true;
    m_selectAll = selectAll;

    return Result<void>::success();
}

void PathField::readProperties(json::ObjectReader& reader) {
    std::string value = m_value;
    reader.read("value", value, json::Presence::Optional);

    if (value != m_value) {
        m_value = std::move(value);
        m_reload = true;
    }

    reader.readChoice("mode", m_mode, {{"file", "file"}, {"folder", "folder"}}, json::Presence::Optional);
    readText(reader, "placeholder", m_placeholder);
    readText(reader, "pickerTitle", m_pickerTitle);
    reader.read("readOnly", m_readOnly, json::Presence::Optional).read("clearButton", m_clearButton, json::Presence::Optional).read("monospace", m_monospace, json::Presence::Optional).read("browseEnabled", m_browseEnabled, json::Presence::Optional).read("picking", m_picking, json::Presence::Optional);

    if (!reader.contains("filters")) {
        return;
    }

    const json::Json* filters = &json::ObjectReader::emptyList();
    reader.readArray("filters", filters);
    m_filters.clear();

    for (const auto& valueFilter : *filters) {
        Filter filter;
        json::ObjectReader nested(valueFilter, "pathField.filters");
        readText(nested, "name", filter.name);
        nested.read("patterns", filter.patterns);

        if (const auto finished = nested.finish(); !finished.hasValue()) {
            fail(finished.error());
            return;
        }

        m_filters.push_back(std::move(filter));
    }
}

Component::Restore PathField::keep() {
    return kept(m_value, m_reload, m_mode, m_placeholder, m_pickerTitle, m_filters, m_readOnly, m_clearButton, m_monospace, m_browseEnabled, m_picking);
}

Result<void> PathField::validate() const {
    if (m_filters.size() > maximumFilters || (m_mode == "folder" && !m_filters.empty())) {
        return Result<void>::failure({"ui_path_filters_invalid", "A file field accepts a bounded list of filters and a folder field accepts none", ""});
    }

    for (const auto& filter : m_filters) {
        if (filter.name.empty() || filter.patterns.empty() || filter.patterns.size() > maximumPatterns) {
            return Result<void>::failure({"ui_path_filters_invalid", "A file filter requires a name and a bounded list of patterns", ""});
        }

        for (const auto& pattern : filter.patterns) {
            if (pattern.empty() || pattern.find('\0') != std::string::npos) {
                return Result<void>::failure({"ui_path_filters_invalid", "A file filter requires nonempty patterns without NUL", ""});
            }
        }
    }

    return Result<void>::success();
}

ImVec2 PathField::measureContent(RenderContext& context, float availableWidth) {
    return {std::min(availableWidth, context.metric(ThemeMetric::SettingsControlMinimumWidth)), WidgetHelper::controlHeight(context)};
}

void PathField::render(RenderContext& context, const ImRect& bounds) {
    const float button = std::min(bounds.GetWidth(), WidgetHelper::controlHeight(context));
    const float spacing = buttonSpacing * context.scale();
    const ImRect field(bounds.Min, ImVec2(std::max(bounds.Min.x, bounds.Max.x - button - spacing), bounds.Max.y));
    const ImRect browse(ImVec2(bounds.Max.x - button, bounds.Min.y), bounds.Max);
    const std::string placeholder = context.text(m_placeholder);

    if (field.GetWidth() >= 1.0F) {
        const TextFieldOptions options{.placeholder = placeholder, .readOnly = m_readOnly, .clearButton = m_clearButton, .monospace = m_monospace, .reload = m_reload, .focus = m_focus, .selectAll = m_selectAll};
        const TextFieldResult result = WidgetHelper::textField(context, "##path", field, m_value, options);
        m_reload = false;
        m_focus = false;
        m_selectAll = false;

        if (result.changed) {
            context.emit(id(), "change", {{"value", m_value}}, {{"value", "value"}});
        }

        if (result.submitted) {
            context.emit(id(), "submit", {{"value", m_value}});
        }

        if (result.deactivated && !result.submitted) {
            context.emit(id(), "blur", {{"value", m_value}});
        }
    }

    const std::string title = m_pickerTitle.empty() ? context.translate(m_mode == "folder" ? "workpane.paths.choose-folder" : "workpane.paths.choose-file") : context.text(m_pickerTitle);
    const bool picking = m_picking;
    const std::optional<Icon> icon = picking ? std::nullopt : std::optional<Icon>(Icon::Folder);
    ImGui::BeginDisabled(m_readOnly || !m_browseEnabled || m_picking);

    if (WidgetHelper::button(context, "##browse", browse, {}, icon, ButtonVariant::Icon)) {
        json::Json filters = json::Json::array();

        for (const auto& filter : m_filters) {
            filters.push_back({{"name", context.text(filter.name)}, {"patterns", filter.patterns}});
        }

        m_picking = true;
        context.emit(id(), "browse", {{"value", m_value}, {"mode", m_mode}, {"title", title}, {"filters", filters}, {"picking", true}}, {{"picking", "picking"}});
    }

    ImGui::EndDisabled();

    if (picking) {
        Painter::busyRing(*ImGui::GetWindowDrawList(), browse.GetCenter(), context.metric(ThemeMetric::SmallIconSize) / 2.0F, context.time(), context.color(ThemeColor::Accent));
        context.requestFrame();
    }

    WidgetHelper::itemTooltip(context, title);
}

} // namespace workpane::ui
