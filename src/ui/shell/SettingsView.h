#pragma once

#include "ui/shell/SettingsGroup.h"
#include "ui/shell/SettingsSection.h"

#include <imgui_internal.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace workpane::ui {

class RenderContext;
class SurfaceStore;

// The settings destination: a searchable list of categories on the left and the sections of the selected one on the right.
class SettingsView final {
  public:
    using SectionRequest = std::function<void(const SettingsGroup& group, const SettingsSection& section)>;

    void setGroups(std::vector<SettingsGroup> groups);
    [[nodiscard]] const std::vector<SettingsGroup>& groups() const;
    void focusSearch();
    void markFailed(const std::string& surface);
    void draw(RenderContext& context, SurfaceStore& surfaces, const ImRect& bounds, const SectionRequest& request);

  private:
    static constexpr float searchHorizontalPadding{12.0F};
    static constexpr float searchVerticalPadding{8.0F};
    static constexpr float categoriesPadding{8.0F};
    static constexpr float titleSpacing{10.0F};
    static constexpr const char* identityScope{"settings-categories"};

    [[nodiscard]] const std::vector<std::size_t>& order(RenderContext& context);
    [[nodiscard]] bool matches(RenderContext& context, const SettingsGroup& group) const;
    void drawCategories(RenderContext& context, const ImRect& bounds, const std::vector<std::size_t>& visible);
    void drawPage(RenderContext& context, SurfaceStore& surfaces, const ImRect& bounds, const SettingsGroup& group, const SectionRequest& request);
    [[nodiscard]] static std::string key(const SettingsGroup& group);

    std::vector<SettingsGroup> m_groups;
    std::vector<std::size_t> m_order;
    std::uint64_t m_orderGeneration{0};
    std::string m_selected;
    std::string m_query;
    std::set<std::string, std::less<>> m_requested;
    std::set<std::string, std::less<>> m_failed;
    bool m_focusSearch{false};
};

} // namespace workpane::ui
