#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/Color.h"
#include "ui/FindBar.h"
#include "ui/Fonts.h"
#include "ui/PseudoTerminal.h"
#include "ui/components/terminal/TerminalFinder.h"
#include "ui/components/terminal/TerminalKeys.h"
#include "ui/components/terminal/TerminalPoint.h"
#include "ui/components/terminal/TerminalScreen.h"
#include "ui/components/terminal/TerminalSelection.h"
#include "ui/model/CommonProperties.h"
#include "ui/model/Component.h"
#include "ui/model/ContentFontSize.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/theme/FontRole.h"

#include <imgui_internal.h>
#include <vterm.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::ui {

// The shell of the reader behind a pseudo-terminal, drawn from a libvterm screen with its history, its selection and its search.
// It starts when it is first drawn, keeps running while it stays mounted and ends its shell when it is detached.
class Terminal final : public Component {
  public:
    explicit Terminal(NodeId id);
    ~Terminal() override;

    [[nodiscard]] std::string_view kind() const override;
    [[nodiscard]] Result<void> command(RenderContext& context, std::string_view name, const json::Json& arguments) override;
    void detach(RenderContext& context) override;
    void update(RenderContext& context) override;

  protected:
    [[nodiscard]] Alignment defaultRowAlignment() const override;
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] Result<void> validate() const override;
    void applied() override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    static constexpr float minimumHeight{120.0F};
    static constexpr float contentPadding{6.0F};
    static constexpr std::int64_t shortestBlink{100};
    static constexpr std::int64_t longestBlink{2000};
    static constexpr double blinkPause{15.0};
    static constexpr float faintShare{0.5F};
    static constexpr double directorySeconds{0.5};
    static constexpr double findRefreshSeconds{0.25};
    static constexpr int wheelLines{3};
    static constexpr std::int64_t maximumHistory{100000};
    static constexpr std::int64_t defaultHistory{10000};
    static constexpr float scrollbarWidth{4.0F};
    static constexpr float scrollbarReach{12.0F};
    static constexpr std::size_t outputSlice{128U * 1024U};
    static constexpr std::array<int, 3> reportedButtons{1, 3, 2};
    static constexpr const char* menuName{"##terminalMenu"};
    static constexpr std::array<std::string_view, 3> linkSchemes{"https://", "http://", "file://"};
    static constexpr int hiddenColumns{80};
    static constexpr int hiddenRows{24};

    struct Glyph final {
        int column;
        std::string text;
    };

    struct Run final {
        FontFace face;
        Color color;
        std::vector<Glyph> glyphs;
    };

    [[nodiscard]] static FontFace face(const VTermScreenCellAttrs& attrs);
    [[nodiscard]] static std::string_view failureKey(std::string_view code);

    void start(RenderContext& context, int columns, int rows);
    void drain(RenderContext& context);
    void follow();
    void refreshFind(RenderContext& context);
    void focus(RenderContext& context, ImGuiID item, const ImRect& bounds);
    void keyboard(RenderContext& context);
    void receive(RenderContext& context, const ImRect& bounds);
    void mouse(RenderContext& context, const ImRect& text, bool scrolling);
    void report(ImVec2 moved);
    void menu(RenderContext& context);
    void perform(RenderContext& context, TerminalKeys::Action action);
    void send(RenderContext& context, std::string_view bytes);
    void paste(RenderContext& context);
    void copy();
    void openFind(RenderContext& context);
    void revealFind(RenderContext& context);
    void reportFind(RenderContext& context);
    void drawCells(RenderContext& context, const ImRect& text);
    void flush(RenderContext& context, const ImRect& text, float top, Run& run) const;
    [[nodiscard]] bool blinking(const RenderContext& context) const;
    void drawCursor(RenderContext& context, const ImRect& text);
    void placeCaret(const ImRect& text) const;
    [[nodiscard]] bool scrollbar(RenderContext& context, const ImRect& bounds);
    void drawFind(RenderContext& context, const ImRect& bounds);
    [[nodiscard]] TerminalPoint point(ImVec2 position, const ImRect& text) const;
    [[nodiscard]] long firstLine() const;
    [[nodiscard]] std::optional<std::string> link(TerminalPoint point) const;

    std::string m_directory;
    std::filesystem::path m_shell;
    std::filesystem::path m_historyFile;
    double m_fontSize{ContentFontSize::standard};
    std::string m_fontFamily{Fonts::bundledMonospace};
    std::string m_palette{"balanced"};
    std::int64_t m_history{defaultHistory};
    bool m_clipboardWrites{false};
    std::int64_t m_cursorBlink{0};
    double m_blinkStart{0.0};
    std::unique_ptr<TerminalScreen> m_screen;
    std::unique_ptr<PseudoTerminal> m_process;
    std::string m_failure;
    std::string m_reportedDirectory;
    std::string m_appliedPalette;
    ImVec2 m_cell;
    double m_directoryCheck{0.0};
    bool m_directoryStale{false};
    double m_findRefresh{0.0};
    bool m_findStale{false};
    std::uint64_t m_drawnFrame{0};
    long m_offset{0};
    ImVec2 m_wheel;
    float m_scrolled{0.0F};
    std::optional<float> m_scrollGrab;
    bool m_writeRefused{false};
    long m_seenHistory{0};
    bool m_exited{false};
    bool m_focused{false};
    bool m_focusRequested{false};
    bool m_selecting{false};
    bool m_pasteable{false};
    TerminalSelection m_selection;
    TerminalFinder m_finder;
    FindBar m_findBar;
};

} // namespace workpane::ui
