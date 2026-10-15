#pragma once

#include "ui/IconCatalog.h"
#include "ui/theme/Theme.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

namespace workpane::ui {

class RenderContext;

enum class Severity { Information, Success, Warning, Error };

// Notifications stack in the bottom corner above the content without ever moving it, at most four at a time.
// Each one stays for its lifetime from the first frame that draws it, so a notice posted before the workspace is on screen is still read in full.
class ToastOverlay final {
  public:
    static constexpr std::size_t maximumVisible{4};
    static constexpr double lifetimeSeconds{6.0};
    static constexpr double fadeSeconds{0.18};

    void show(std::string title, std::string message, Severity severity, double now);
    void draw(RenderContext& context, float windowWidth, float windowHeight);
    [[nodiscard]] std::size_t liveCount() const;
    [[nodiscard]] bool showing(std::string_view title, std::string_view message) const;

  private:
    static constexpr float toastPaddingLeft{13.0F};
    static constexpr float toastPaddingRight{8.0F};
    static constexpr float toastPaddingVertical{11.0F};
    static constexpr float toastSpacing{11.0F};
    static constexpr float toastIconSize{18.0F};
    static constexpr float toastCloseSize{16.0F};
    static constexpr float toastCloseIcon{12.0F};
    static constexpr float toastSeverityBorder{3.0F};
    static constexpr float toastRadius{3.0F};
    static constexpr float toastLineSpacing{2.0F};

    [[nodiscard]] static Icon icon(Severity severity);
    [[nodiscard]] static ThemeColor color(Severity severity);

    struct Toast final {
        std::uint64_t id{0};
        std::string title;
        std::string message;
        Severity severity{Severity::Information};
        double shown{-1.0};
        double dismissed{-1.0};
    };

    [[nodiscard]] float opacity(const Toast& toast, double now) const;
    void dismiss(Toast& toast, double now);

    std::deque<Toast> m_toasts;
    std::uint64_t m_nextId{1};
};

} // namespace workpane::ui
