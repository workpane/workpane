#pragma once

#include "ui/ButtonState.h"
#include "ui/Color.h"
#include "ui/IconCatalog.h"
#include "ui/RowContent.h"
#include "ui/TextAreaOptions.h"
#include "ui/TextFieldOptions.h"
#include "ui/TextFieldResult.h"
#include "ui/theme/FontRole.h"

#include <imgui_internal.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::ui {

class RenderContext;

enum class ButtonVariant { Default, Primary, Destructive, Toolbar, Icon, Link };

enum class TextAlign { Start, Center, End };

enum class RowStyle { Default, Navigation };

// The one implementation of every control the product draws, shared by Lua components and by the shell so both look identical.
class Widgets final {
  public:
    [[nodiscard]] static bool disabled();
    static void takeKeyboard();
    static void takeNavigation();
    [[nodiscard]] static ImU32 ink(Color color);
    [[nodiscard]] static float controlHeight(const RenderContext& context);
    [[nodiscard]] static float fontSize(const RenderContext& context, FontRole role);
    [[nodiscard]] static ImVec2 textSize(const RenderContext& context, FontRole role, std::string_view text, float wrapWidth = -1.0F);
    static void text(const RenderContext& context, ImDrawList& list, FontRole role, ImVec2 position, Color color, std::string_view text, float wrapWidth = -1.0F);
    static void alignedText(const RenderContext& context, ImDrawList& list, FontRole role, const ImRect& bounds, Color color, std::string_view text, TextAlign align);
    [[nodiscard]] static std::string elided(const RenderContext& context, FontRole role, std::string_view text, float width);
    [[nodiscard]] static float lineHeight(const RenderContext& context, FontRole role);
    [[nodiscard]] static std::vector<std::string_view> wrapLines(const RenderContext& context, FontRole role, std::string_view text, float width);
    [[nodiscard]] static ImVec2 paragraphSize(const RenderContext& context, FontRole role, std::string_view text, float width);
    static void paragraph(const RenderContext& context, ImDrawList& list, FontRole role, const ImRect& bounds, Color color, std::string_view text, TextAlign align);

    [[nodiscard]] static ButtonState interact(ImGuiID id, const ImRect& bounds, ImGuiButtonFlags flags = ImGuiButtonFlags_None);
    [[nodiscard]] static ImVec2 buttonSize(const RenderContext& context, std::string_view label, std::optional<Icon> icon, ButtonVariant variant);
    static bool button(const RenderContext& context, std::string_view id, const ImRect& bounds, std::string_view label, std::optional<Icon> icon, ButtonVariant variant, bool checked = false);
    static bool closeCircle(const RenderContext& context, std::string_view id, ImVec2 center, float diameter);
    static TextFieldResult textField(const RenderContext& context, std::string_view id, const ImRect& bounds, std::string& value, const TextFieldOptions& options);
    static TextFieldResult textArea(const RenderContext& context, std::string_view id, const ImRect& bounds, std::string& value, const TextAreaOptions& options);
    static bool combo(const RenderContext& context, std::string_view id, const ImRect& bounds, const std::vector<std::string>& items, int& selected, std::string_view placeholder);
    static bool checkbox(const RenderContext& context, std::string_view id, const ImRect& bounds, bool& value, std::string_view label);
    static bool toggle(RenderContext& context, std::string_view id, const ImRect& bounds, bool& value);
    static bool radio(const RenderContext& context, std::string_view id, const ImRect& bounds, bool selected, std::string_view label);
    static bool slider(const RenderContext& context, std::string_view id, const ImRect& bounds, double& value, double minimum, double maximum, double step);
    [[nodiscard]] static ImVec2 checkboxSize(const RenderContext& context, std::string_view label);
    [[nodiscard]] static ImVec2 toggleSize(const RenderContext& context);
    static void pageHeader(const RenderContext& context, const ImRect& bounds, std::string_view title, std::string_view caption);
    [[nodiscard]] static float pageHeaderLeading(const RenderContext& context, std::string_view title, std::string_view caption);
    static void sectionTitle(const RenderContext& context, ImDrawList& list, const ImRect& bounds, std::string_view text);
    [[nodiscard]] static float listRowHeight(const RenderContext& context);
    static ButtonState listRow(const RenderContext& context, ImGuiID id, const ImRect& bounds, const RowContent& content, bool selected, RowStyle style);
    static void focusBorder(const RenderContext& context, const ImRect& bounds);
    static void frame(const RenderContext& context, const ImRect& bounds, Color fill);
    [[nodiscard]] static bool beginTooltip(const RenderContext& context);
    static void endTooltip();
    [[nodiscard]] static float tooltipWidth(const RenderContext& context);
    static void tooltip(const RenderContext& context, std::string_view text);
    static void itemTooltip(RenderContext& context, std::string_view text);
    static void placePopup(const RenderContext& context, std::string_view name, const ImRect& anchor);

  private:
    static constexpr float buttonContentSpacing{5.0F};
    static constexpr float toolbarHorizontalPadding{6.0F};
    static constexpr float choiceBoxSize{16.0F};
    static constexpr float choiceLabelSpacing{8.0F};
    static constexpr float toggleTrackWidth{34.0F};
    static constexpr float toggleTrackHeight{18.0F};
    static constexpr float toggleKnobInset{2.0F};
    static constexpr float toggleSpeed{9.0F};
    static constexpr float sliderGrooveHeight{4.0F};
    static constexpr float sliderKnobSize{14.0F};
    static constexpr float clearButtonSize{16.0F};
    static constexpr float clearButtonMargin{6.0F};
    static constexpr float comboChevronWidth{9.0F};
    static constexpr int comboVisibleItems{10};
    static constexpr float textAreaHorizontalPadding{7.0F};
    static constexpr float textAreaVerticalPadding{4.0F};
    static constexpr const char* ellipsis{"\xE2\x80\xA6"};
    static constexpr float fitTolerance{0.01F};
    static constexpr float pageHeaderLeft{14.0F};
    static constexpr float pageHeaderGap{8.0F};
    static constexpr float listPadding{9.0F};
    static constexpr float listRowPadding{12.0F};
    static constexpr float glyphSpacing{6.0F};
    static constexpr float navigationMarker{2.0F};
    static constexpr float tooltipMaximumWidth{360.0F};
    static constexpr float popupGap{2.0F};

    [[nodiscard]] static ImFont* font(const RenderContext& context, FontRole role);
    static int submitOnEnter(ImGuiInputTextCallbackData* data);
    [[nodiscard]] static bool focused();
};

} // namespace workpane::ui
