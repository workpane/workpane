#pragma once

#include "ui/shell/DialogButton.h"
#include "ui/shell/DialogRequest.h"

#include <imgui.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace workpane::ui {

class RenderContext;
class SurfaceStore;
struct Surface;

// Dialogs of the product are modal over a dimmed window, and one asked for while another is open is drawn above it and answered first.
class DialogHost final {
  public:
    static constexpr const char* confirmButton{"confirm"};
    static constexpr const char* cancelButton{"cancel"};

    void open(DialogRequest request);
    void draw(RenderContext& context, SurfaceStore& surfaces, ImVec2 windowSize);
    [[nodiscard]] bool active() const;
    void cancelAll();
    void dismiss(const std::string& surface, const std::string& button);
    void dismissOwner(const std::string& owner);
    void replaceButtons(const std::string& surface, std::vector<DialogButton> buttons);

  private:
    static constexpr float dialogHorizontalPadding{22.0F};
    static constexpr float dialogVerticalPadding{20.0F};
    static constexpr float dialogSpacing{8.0F};
    static constexpr float dialogButtonGap{10.0F};
    static constexpr float dialogRadius{4.0F};
    static constexpr float dialogMargin{24.0F};

    struct Entry final {
        DialogRequest request;
        bool focusRequested{true};
        std::string dismissal;
        std::size_t serial{0};
        std::optional<float> height;
    };

    [[nodiscard]] static DialogButton closing(std::string id, std::string text, ButtonVariant variant);
    [[nodiscard]] static bool hasBody(const DialogRequest& request);
    [[nodiscard]] static float bodyHeight(RenderContext& context, Surface* surface, const DialogRequest& request, float width);

    void drawLevel(RenderContext& context, SurfaceStore& surfaces, ImVec2 windowSize, std::size_t level);
    [[nodiscard]] std::string drawBody(RenderContext& context, Surface* surface, Entry& entry, float visible);
    void close(std::size_t level, const std::string& button);
    [[nodiscard]] std::vector<DialogButton> buttons(const DialogRequest& request) const;

    std::vector<Entry> m_stack;
    std::size_t m_opened{0};
    bool m_covered{false};
};

} // namespace workpane::ui
