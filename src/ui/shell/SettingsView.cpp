#include "ui/shell/SettingsView.h"

#include "localization/Localization.h"
#include "ui/ButtonState.h"
#include "ui/Painter.h"
#include "ui/RowContent.h"
#include "ui/TextCaseHelper.h"
#include "ui/TextFieldOptions.h"
#include "ui/WidgetHelper.h"
#include "ui/model/RenderContext.h"
#include "ui/model/Surface.h"
#include "ui/model/SurfaceStore.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <optional>
#include <set>
#include <utility>

namespace workpane::ui {

// The requests and failures of sections whose owner left are forgotten, so the owner builds them again when it starts once more.
void SettingsView::setGroups(std::vector<SettingsGroup> groups) {
    m_groups = std::move(groups);
    m_orderGeneration = 0;
    std::set<std::string, std::less<>> present;

    for (const auto& group : m_groups) {
        for (const auto& section : group.sections) {
            present.insert(group.surface(section));
        }
    }

    // clang-format off
    std::erase_if(m_requested, [&present](const std::string& surface) { return !present.contains(surface); });
    std::erase_if(m_failed, [&present](const std::string& surface) { return !present.contains(surface); });
    // clang-format on
}

const std::vector<SettingsGroup>& SettingsView::groups() const {
    return m_groups;
}

void SettingsView::focusSearch() {
    m_focusSearch = true;
}

void SettingsView::markFailed(const std::string& surface) {
    m_failed.insert(surface);
}

void SettingsView::draw(RenderContext& context, SurfaceStore& surfaces, const ImRect& bounds, const SectionRequest& request) {
    const float scale = context.scale();
    const float header = context.metric(ThemeMetric::PageHeaderHeight);
    WidgetHelper::pageHeader(context, ImRect(bounds.Min, ImVec2(bounds.Max.x, bounds.Min.y + header)), context.translate("workpane.settings.title"), {});

    const float band = WidgetHelper::controlHeight(context) + searchVerticalPadding * 2.0F * scale;
    const ImRect search(bounds.Min.x, bounds.Min.y + header, bounds.Max.x, bounds.Min.y + header + band);
    ImDrawList& list = *ImGui::GetWindowDrawList();
    list.AddRectFilled(search.Min, search.Max, WidgetHelper::ink(context.color(ThemeColor::Panel)));
    Painter::horizontalDivider(list, ImVec2(search.Min.x, search.Max.y - context.metric(ThemeMetric::LineWidth)), search.GetWidth(), context.metric(ThemeMetric::LineWidth), context.color(ThemeColor::Border));

    const std::string placeholder = context.translate("workpane.settings.search");
    const ImRect field(search.Min.x + searchHorizontalPadding * scale, search.Min.y + searchVerticalPadding * scale, search.Max.x - searchHorizontalPadding * scale, search.Max.y - searchVerticalPadding * scale - context.metric(ThemeMetric::LineWidth));
    const TextFieldOptions options{.placeholder = placeholder, .clearButton = true, .focus = m_focusSearch, .selectAll = m_focusSearch};
    WidgetHelper::textField(context, "##settingsSearch", field, m_query, options);
    m_focusSearch = false;

    const ImRect body(bounds.Min.x, search.Max.y, bounds.Max.x, bounds.Max.y);
    const auto& ordered = order(context);
    std::vector<std::size_t> visible;

    for (const std::size_t index : ordered) {
        if (matches(context, m_groups[index])) {
            visible.push_back(index);
        }
    }

    if (visible.empty()) {
        WidgetHelper::paragraph(context, list, context.font(ThemeFont::Interface), ImRect(body.Min.x, body.GetCenter().y, body.Max.x, body.Max.y), context.color(ThemeColor::TextMuted), context.translate("workpane.settings.no-results"), TextAlign::Center);
        return;
    }

    // A search hiding the selected category moves the selection to the first category still visible.
    // clang-format off
    const bool selectionVisible = std::ranges::any_of(visible, [this](std::size_t index) { return key(m_groups[index]) == m_selected; });
    // clang-format on

    if (!selectionVisible) {
        m_selected = key(m_groups[visible.front()]);
    }

    const float categoriesWidth = context.metric(ThemeMetric::SettingsCategoryWidth);
    drawCategories(context, ImRect(body.Min, ImVec2(body.Min.x + categoriesWidth, body.Max.y)), visible);

    for (const std::size_t index : visible) {
        if (key(m_groups[index]) == m_selected) {
            drawPage(context, surfaces, ImRect(ImVec2(body.Min.x + categoriesWidth, body.Min.y), body.Max), m_groups[index], request);
        }
    }
}

// Every group, the ones of the core among them, is listed alphabetically in the language being read.
const std::vector<std::size_t>& SettingsView::order(RenderContext& context) {
    if (m_orderGeneration == context.localization().generation() && m_order.size() == m_groups.size()) {
        return m_order;
    }

    m_order.resize(m_groups.size());
    std::iota(m_order.begin(), m_order.end(), std::size_t{0});
    // clang-format off
    std::ranges::stable_sort(m_order, [this, &context](std::size_t left, std::size_t right) {
        return TextCaseHelper::alphabeticalLess(context.translate(m_groups[left].titleKey), context.translate(m_groups[right].titleKey));
    });
    // clang-format on

    m_orderGeneration = context.localization().generation();

    return m_order;
}

bool SettingsView::matches(RenderContext& context, const SettingsGroup& group) const {
    const auto first = m_query.find_first_not_of(' ');

    if (first == std::string::npos) {
        return true;
    }

    const std::string query = TextCaseHelper::lower(m_query.substr(first, m_query.find_last_not_of(' ') - first + 1));
    std::string searchable = context.translate(group.titleKey);

    for (const auto& section : group.sections) {
        searchable += " " + context.translate(section.titleKey);

        for (const auto& searchKey : section.searchKeys) {
            searchable += " " + context.translate(searchKey);
        }
    }

    return TextCaseHelper::lower(searchable).find(query) != std::string::npos;
}

void SettingsView::drawCategories(RenderContext& context, const ImRect& bounds, const std::vector<std::size_t>& visible) {
    ImDrawList& list = *ImGui::GetWindowDrawList();
    list.AddRectFilled(bounds.Min, bounds.Max, WidgetHelper::ink(context.color(ThemeColor::Panel)));
    Painter::verticalDivider(list, ImVec2(bounds.Max.x - context.metric(ThemeMetric::LineWidth), bounds.Min.y), bounds.GetHeight(), context.metric(ThemeMetric::LineWidth), context.color(ThemeColor::Border));

    const float padding = categoriesPadding * context.scale();
    const float height = WidgetHelper::listRowHeight(context);
    float y = bounds.Min.y + padding;

    for (const std::size_t index : visible) {
        const SettingsGroup& group = m_groups[index];
        const std::string identity = key(group);
        const ImRect row(bounds.Min.x + padding, y, bounds.Max.x - padding - context.metric(ThemeMetric::LineWidth), y + height);
        const std::string title = context.translate(group.titleKey);
        ImGui::PushID(identityScope);
        const ImGuiID id = ImGui::GetID(identity.data(), identity.data() + identity.size());
        ImGui::PopID();
        const ButtonState state = WidgetHelper::listRow(context, id, row, RowContent{title, {}, std::nullopt, std::nullopt}, identity == m_selected, RowStyle::Navigation);

        if (state.pressed) {
            m_selected = identity;
        }

        y += height;
    }
}

void SettingsView::drawPage(RenderContext& context, SurfaceStore& surfaces, const ImRect& bounds, const SettingsGroup& group, const SectionRequest& request) {
    // Each group scrolls on its own, so coming back to one finds it where the reader left it.
    ImGui::SetCursorScreenPos(bounds.Min);
    const std::string page = "##settingsPage:" + group.owner + ":" + group.id;

    if (!ImGui::BeginChild(page.c_str(), bounds.GetSize(), ImGuiChildFlags_None, ImGuiWindowFlags_None)) {
        ImGui::EndChild();
        return;
    }

    const float scale = context.scale();
    const float inset = context.metric(ThemeMetric::SettingsHorizontalPadding);
    const float spacing = context.metric(ThemeMetric::SettingsSectionSpacing);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = bounds.GetWidth() - (ImGui::GetScrollMaxY() > 0.0F ? ImGui::GetStyle().ScrollbarSize : 0.0F);
    ImDrawList& list = *ImGui::GetWindowDrawList();
    float y = origin.y + context.metric(ThemeMetric::SettingsVerticalPadding);

    for (std::size_t index = 0; index < group.sections.size(); ++index) {
        const SettingsSection& section = group.sections[index];

        // Exactly one divider separates two sections, so a group of one section carries none.
        if (index > 0) {
            Painter::horizontalDivider(list, ImVec2(origin.x, y), width, context.metric(ThemeMetric::LineWidth), context.color(ThemeColor::Border));
            y += 1.0F + spacing;
        }

        const float titleHeight = WidgetHelper::lineHeight(context, context.font(ThemeFont::SectionTitle));
        WidgetHelper::sectionTitle(context, list, ImRect(origin.x + inset, y, origin.x + width - inset, y + titleHeight), context.translate(section.titleKey));
        y += titleHeight + titleSpacing * scale;

        const std::string surfaceId = group.surface(section);

        if (Surface* surface = surfaces.find(surfaceId); surface != nullptr) {
            context.setSurface(surface->id, surface->assets);
            const ImVec2 size = surface->root->measure(context, width);
            surface->root->draw(context, ImRect(origin.x, y, origin.x + width, y + size.y));
            y += size.y;
        } else if (m_failed.contains(surfaceId)) {
            const FontRole font = context.font(ThemeFont::Interface);
            const std::string failure = context.translate("workpane.view.failed-message");
            const ImVec2 size = WidgetHelper::paragraphSize(context, font, failure, width - inset * 2.0F);
            WidgetHelper::paragraph(context, list, font, ImRect(origin.x + inset, y, origin.x + width - inset, y + size.y), context.color(ThemeColor::DangerText), failure, TextAlign::Start);
            y += size.y;
        } else if (m_requested.insert(surfaceId).second) {
            request(group, section);
        }

        y += spacing;
    }

    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(width, y - origin.y));
    ImGui::EndChild();
}

std::string SettingsView::key(const SettingsGroup& group) {
    return group.owner + ":" + group.id;
}

} // namespace workpane::ui
