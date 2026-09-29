#pragma once

#include "ui/Color.h"
#include "ui/components/terminal/TerminalCursor.h"
#include "ui/components/terminal/TerminalPalette.h"
#include "ui/components/terminal/TerminalSignals.h"

#include <vterm.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace workpane::ui {

// A libvterm screen with a bounded history above it, which keeps every line the program scrolled away and knows which lines continue on the next one.
// Lines are counted from the oldest line of the history, so the history comes first and the rows on screen follow it.
class TerminalScreen final {
  public:
    TerminalScreen(int rows, int columns, std::size_t historyLimit);
    ~TerminalScreen();

    TerminalScreen(const TerminalScreen&) = delete;
    TerminalScreen& operator=(const TerminalScreen&) = delete;

    void write(std::string_view bytes);
    void resize(int rows, int columns);
    void setPalette(const TerminalPalette& palette);
    void setHistoryLimit(std::size_t limit);
    void key(VTermKey key, VTermModifier modifiers);
    void character(std::uint32_t character, VTermModifier modifiers);
    void paste(std::string_view text);
    void mouseMove(int row, int column, VTermModifier modifiers);
    void mouseButton(int button, bool pressed, VTermModifier modifiers);
    void focus(bool focused);
    void clearHistory();
    void reset();

    [[nodiscard]] std::string takeReply();
    [[nodiscard]] TerminalSignals takeSignals();

    [[nodiscard]] int rows() const;
    [[nodiscard]] int columns() const;
    [[nodiscard]] long history() const;
    [[nodiscard]] long lines() const;
    [[nodiscard]] std::uint64_t dropped() const;
    [[nodiscard]] VTermScreenCell cell(long line, int column) const;
    [[nodiscard]] bool wraps(long line) const;
    [[nodiscard]] std::string text(long line, int from, int to) const;
    [[nodiscard]] std::string link(const VTermScreenCell& cell) const;
    [[nodiscard]] Color color(VTermColor color) const;
    [[nodiscard]] const TerminalCursor& cursor() const;
    [[nodiscard]] int mouseMode() const;
    [[nodiscard]] bool alternateScreen() const;

  private:
    static constexpr int directoryCommand{7};
    static constexpr int linkCommand{8};
    static constexpr int messageCommand{9};
    static constexpr int notifyCommand{777};
    static constexpr std::size_t maximumLinks{65535};
    static constexpr std::size_t maximumLinkBytes{1U << 22U};
    static constexpr std::size_t largestTitle{4096};
    static constexpr std::size_t largestCommand{65536};
    static constexpr std::size_t largestClipboard{1U << 20U};

    struct Gathered final {
        std::string text;
        bool overflowed{false};
    };

    struct HistoryLine final {
        std::vector<VTermScreenCell> cells;
        bool wraps{false};
    };

    static int cursorMoved(VTermPos position, VTermPos previous, int visible, void* user);
    static int propertyChanged(VTermProp property, VTermValue* value, void* user);
    static int rang(void* user);
    static int pushed(int columns, const VTermScreenCell* cells, void* user);
    static int popped(int columns, VTermScreenCell* cells, void* user);
    static int cleared(void* user);
    static int commanded(int command, VTermStringFragment fragment, void* user);
    void notify(int command, const std::string& text);
    void startLink(std::string_view command);
    static int selected(VTermSelectionMask mask, VTermStringFragment fragment, void* user);
    static void replied(const char* bytes, std::size_t length, void* user);
    [[nodiscard]] static bool gather(Gathered& gathered, VTermStringFragment fragment, std::size_t bound);
    [[nodiscard]] static std::string utf8(const VTermScreenCell& cell);
    [[nodiscard]] static std::string directory(std::string_view address);
    [[nodiscard]] VTermScreenCell blank() const;

    VTerm* m_terminal{nullptr};
    VTermScreen* m_screen{nullptr};
    VTermState* m_state{nullptr};
    VTermScreenCallbacks m_callbacks{};
    VTermStateFallbacks m_fallbacks{};
    VTermSelectionCallbacks m_selection{};
    std::array<char, 8192> m_selectionBuffer{};
    std::deque<HistoryLine> m_history;
    std::size_t m_historyLimit;
    std::uint64_t m_dropped{0};
    std::string m_reply;
    TerminalSignals m_signals;
    TerminalCursor m_cursor;
    int m_mouseMode{0};
    bool m_alternate{false};
    Gathered m_title;
    Gathered m_command;
    Gathered m_clipboard;
    std::deque<std::string> m_links;
    std::unordered_map<std::string_view, std::size_t> m_linkNumbers;
    std::size_t m_linkBytes{0};
};

} // namespace workpane::ui
