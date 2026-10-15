#include "ui/theme/Style.h"

#include <imgui_internal.h>

namespace workpane::ui {

void Style::apply(const Theme& theme, const Fonts& fonts, float scale, ImGuiStyle& style) {
    style = ImGuiStyle();

    // Geometry follows the flat standard: square windows, the control radius on fields and no border ImGui would draw on its own.
    style.WindowPadding = ImVec2(0.0F, 0.0F);
    style.WindowRounding = 0.0F;
    style.WindowBorderSize = 0.0F;
    style.ChildRounding = 0.0F;
    style.ChildBorderSize = 0.0F;
    style.PopupRounding = theme.metric(ThemeMetric::ControlRadius);
    style.PopupBorderSize = 1.0F;
    style.FramePadding = ImVec2(theme.metric(ThemeMetric::ControlHorizontalPadding), theme.metric(ThemeMetric::ControlVerticalPadding));
    style.FrameRounding = theme.metric(ThemeMetric::ControlRadius);
    style.FrameBorderSize = 0.0F;
    style.ItemSpacing = ImVec2(theme.metric(ThemeMetric::ItemSpacing), theme.metric(ThemeMetric::ItemSpacing));
    style.ItemInnerSpacing = ImVec2(6.0F, 6.0F);
    style.CellPadding = ImVec2(7.0F, 4.0F);
    style.IndentSpacing = 18.0F;
    style.ScrollbarSize = theme.metric(ThemeMetric::ScrollBarExtent);
    style.ScrollbarRounding = 4.0F;
    style.ScrollbarPadding = 1.0F;
    style.GrabMinSize = 28.0F;
    style.GrabRounding = theme.metric(ThemeMetric::ControlRadius);
    style.TabRounding = 0.0F;
    style.TabBorderSize = 0.0F;
    style.SeparatorTextBorderSize = 1.0F;
    style.SelectableTextAlign = ImVec2(0.0F, 0.5F);
    style.DisabledAlpha = 0.45F;
    style.AntiAliasedLines = true;
    style.AntiAliasedFill = true;
    const FontRole interface = theme.font(ThemeFont::Interface);
    style.FontSizeBase = fonts.size(interface.face, interface.size);

    const Color window = theme.color(ThemeColor::Window);
    const Color panel = theme.color(ThemeColor::Panel);
    const Color raised = theme.color(ThemeColor::Raised);
    const Color hover = theme.color(ThemeColor::Hover);
    const Color pressed = theme.color(ThemeColor::Pressed);
    const Color border = theme.color(ThemeColor::Border);
    const Color borderStrong = theme.color(ThemeColor::BorderStrong);
    const Color text = theme.color(ThemeColor::Text);
    const Color accent = theme.color(ThemeColor::Accent);
    const Color accentText = theme.color(ThemeColor::AccentText);
    ImVec4* colors = style.Colors;

    colors[ImGuiCol_Text] = text.vector();
    colors[ImGuiCol_TextDisabled] = theme.color(ThemeColor::TextDisabled).vector();
    colors[ImGuiCol_WindowBg] = window.vector();
    colors[ImGuiCol_ChildBg] = window.withAlpha(0.0F).vector();
    colors[ImGuiCol_PopupBg] = panel.vector();
    colors[ImGuiCol_Border] = border.vector();
    colors[ImGuiCol_BorderShadow] = border.withAlpha(0.0F).vector();
    colors[ImGuiCol_FrameBg] = raised.vector();
    colors[ImGuiCol_FrameBgHovered] = hover.vector();
    colors[ImGuiCol_FrameBgActive] = pressed.vector();
    colors[ImGuiCol_TitleBg] = panel.vector();
    colors[ImGuiCol_TitleBgActive] = panel.vector();
    colors[ImGuiCol_TitleBgCollapsed] = panel.vector();
    colors[ImGuiCol_MenuBarBg] = panel.vector();
    colors[ImGuiCol_ScrollbarBg] = window.withAlpha(0.0F).vector();
    colors[ImGuiCol_ScrollbarGrab] = theme.color(ThemeColor::Scrollbar).vector();
    colors[ImGuiCol_ScrollbarGrabHovered] = theme.color(ThemeColor::ScrollbarHover).vector();
    colors[ImGuiCol_ScrollbarGrabActive] = theme.color(ThemeColor::ScrollbarActive).vector();
    colors[ImGuiCol_CheckMark] = accent.vector();
    colors[ImGuiCol_SliderGrab] = accent.vector();
    colors[ImGuiCol_SliderGrabActive] = accentText.vector();
    colors[ImGuiCol_Button] = raised.vector();
    colors[ImGuiCol_ButtonHovered] = hover.vector();
    colors[ImGuiCol_ButtonActive] = borderStrong.vector();
    colors[ImGuiCol_Header] = accent.vector();
    colors[ImGuiCol_HeaderHovered] = hover.vector();
    colors[ImGuiCol_HeaderActive] = accent.vector();
    colors[ImGuiCol_Separator] = border.vector();
    colors[ImGuiCol_SeparatorHovered] = accent.vector();
    colors[ImGuiCol_SeparatorActive] = accentText.vector();
    colors[ImGuiCol_ResizeGrip] = window.withAlpha(0.0F).vector();
    colors[ImGuiCol_ResizeGripHovered] = accent.vector();
    colors[ImGuiCol_ResizeGripActive] = accentText.vector();
    colors[ImGuiCol_InputTextCursor] = text.vector();
    colors[ImGuiCol_Tab] = panel.vector();
    colors[ImGuiCol_TabHovered] = hover.vector();
    colors[ImGuiCol_TabSelected] = window.vector();
    colors[ImGuiCol_TabSelectedOverline] = accent.vector();
    colors[ImGuiCol_TabDimmed] = panel.vector();
    colors[ImGuiCol_TabDimmedSelected] = window.vector();
    colors[ImGuiCol_TabDimmedSelectedOverline] = accent.vector();
    colors[ImGuiCol_PlotLines] = accent.vector();
    colors[ImGuiCol_PlotLinesHovered] = accentText.vector();
    colors[ImGuiCol_PlotHistogram] = accent.vector();
    colors[ImGuiCol_PlotHistogramHovered] = accentText.vector();
    colors[ImGuiCol_TableHeaderBg] = panel.vector();
    colors[ImGuiCol_TableBorderStrong] = border.vector();
    colors[ImGuiCol_TableBorderLight] = border.vector();
    colors[ImGuiCol_TableRowBg] = window.withAlpha(0.0F).vector();
    colors[ImGuiCol_TableRowBgAlt] = panel.vector();
    colors[ImGuiCol_TextLink] = accentText.vector();
    colors[ImGuiCol_TextSelectedBg] = theme.color(ThemeColor::Selection).vector();
    colors[ImGuiCol_TreeLines] = border.vector();
    colors[ImGuiCol_DragDropTarget] = accent.vector();
    colors[ImGuiCol_DragDropTargetBg] = theme.color(ThemeColor::Selection).vector();
    colors[ImGuiCol_UnsavedMarker] = text.vector();
    colors[ImGuiCol_NavCursor] = theme.color(ThemeColor::Focus).vector();
    colors[ImGuiCol_NavWindowingHighlight] = accent.vector();
    colors[ImGuiCol_NavWindowingDimBg] = theme.color(ThemeColor::Overlay).vector();
    colors[ImGuiCol_ModalWindowDimBg] = theme.color(ThemeColor::Overlay).vector();

    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
}

// Curves are tessellated and edges smoothed in pixels of the framebuffer rather than in the points ImGui lays out in, so a switch is as round and as crisp on a display of density two as on any other.
void Style::matchDensity(float density) {
    ImGuiContext& context = *GImGui;
    const float fringe = 1.0F / density;
    context.Style.CircleTessellationMaxError = curveError / density;
    context.DrawListSharedData.SetCircleTessellationMaxError(context.Style.CircleTessellationMaxError);
    context.DrawListSharedData.InitialFringeScale = fringe;
    ImGui::GetBackgroundDrawList()->_FringeScale = fringe;
    ImGui::GetForegroundDrawList()->_FringeScale = fringe;
}

} // namespace workpane::ui
