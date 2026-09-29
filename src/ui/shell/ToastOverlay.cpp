#include "ui/shell/ToastOverlay.h"

#include "ui/ButtonState.h"
#include "ui/Color.h"
#include "ui/IconCatalog.h"
#include "ui/Painter.h"
#include "ui/Widgets.h"
#include "ui/model/RenderContext.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace workpane::ui {

Icon ToastOverlay::icon(Severity severity) {
    switch (severity) {
    case Severity::Information:
        return Icon::Information;
    case Severity::Success:
        return Icon::Success;
    case Severity::Warning:
        return Icon::Warning;
    case Severity::Error:
        return Icon::Error;
    }

    return Icon::Information;
}

ThemeColor ToastOverlay::color(Severity severity) {
    switch (severity) {
    case Severity::Information:
        return ThemeColor::Accent;
    case Severity::Success:
        return ThemeColor::Success;
    case Severity::Warning:
        return ThemeColor::Warning;
    case Severity::Error:
        return ThemeColor::Danger;
    }

    return ThemeColor::Accent;
}

// A fifth notification dismisses the oldest live one, so the stack never grows past what the corner holds.
void ToastOverlay::show(std::string title, std::string message, Severity severity, double now) {
    std::size_t live = liveCount();

    for (auto& toast : m_toasts) {
        if (live < maximumVisible) {
            break;
        }

        if (toast.dismissed < 0.0) {
            dismiss(toast, now);
            --live;
        }
    }

    m_toasts.push_back({m_nextId++, std::move(title), std::move(message), severity, -1.0, -1.0});
}

void ToastOverlay::draw(RenderContext& context, float windowWidth, float windowHeight) {
    const double now = context.time();

    // clang-format off
    std::erase_if(m_toasts, [now](const Toast& toast) { return toast.dismissed >= 0.0 && now - toast.dismissed >= fadeSeconds; });
    // clang-format on

    for (auto& toast : m_toasts) {
        if (toast.shown < 0.0) {
            toast.shown = now;
        }

        if (toast.dismissed < 0.0 && now - toast.shown >= lifetimeSeconds) {
            dismiss(toast, now);
        }
    }

    const float scale = context.scale();
    const float width = context.metric(ThemeMetric::ToastWidth);
    const float margin = context.metric(ThemeMetric::ToastMargin);
    const float spacing = context.metric(ThemeMetric::ToastSpacing);
    const float textLeft = (toastPaddingLeft + toastIconSize + toastSpacing) * scale;
    const float textWidth = width - textLeft - (toastCloseSize + toastPaddingRight + toastSpacing) * scale;
    FontRole titleFont = context.font(ThemeFont::Interface);
    titleFont.face = FontFace::SemiBold;
    const FontRole messageFont = context.font(ThemeFont::Interface);
    float bottom = windowHeight - margin;

    std::size_t slot = 0;

    // A toast takes the window of its place in the stack, since ImGui keeps every window it ever made, so a long session never piles up windows.
    for (auto toast = m_toasts.rbegin(); toast != m_toasts.rend(); ++toast, ++slot) {
        const ImVec2 title = Widgets::paragraphSize(context, titleFont, toast->title, textWidth);
        const ImVec2 message = toast->message.empty() ? ImVec2(0.0F, 0.0F) : Widgets::paragraphSize(context, messageFont, toast->message, textWidth);
        const float content = title.y + (toast->message.empty() ? 0.0F : toastLineSpacing * scale + message.y);
        const float height = std::ceil(std::max(toastIconSize * scale, content) + toastPaddingVertical * 2.0F * scale);
        const ImVec2 origin(windowWidth - margin - width, bottom - height);
        const std::string window = "##toast" + std::to_string(slot);
        const float alpha = opacity(*toast, now);

        ImGui::SetNextWindowPos(origin);
        ImGui::SetNextWindowSize(ImVec2(width, height));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::Begin(window.c_str(), nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground);
        ImDrawList& list = *ImGui::GetWindowDrawList();
        const Color accent = context.color(color(toast->severity));
        const ImVec2 corner(origin.x + width, origin.y + height);
        const float radius = toastRadius * scale;

        list.AddRectFilled(origin, corner, Widgets::ink(context.color(ThemeColor::Raised)), radius);
        Painter::rectBorder(list, origin, corner, radius, context.metric(ThemeMetric::LineWidth), context.color(ThemeColor::Border));
        list.AddRectFilled(origin, ImVec2(origin.x + toastSeverityBorder * scale, corner.y), Widgets::ink(accent), radius, ImDrawFlags_RoundCornersLeft);

        const float top = origin.y + toastPaddingVertical * scale;
        IconCatalog::draw(context.fonts(), list, ToastOverlay::icon(toast->severity), ImFloor(ImVec2(origin.x + toastPaddingLeft * scale, top)), toastIconSize * scale, accent);
        Widgets::paragraph(context, list, titleFont, ImRect(origin.x + textLeft, top, origin.x + textLeft + textWidth, top + title.y), context.color(ThemeColor::Text), toast->title, TextAlign::Start);

        if (!toast->message.empty()) {
            const float messageTop = top + title.y + toastLineSpacing * scale;
            Widgets::paragraph(context, list, messageFont, ImRect(origin.x + textLeft, messageTop, origin.x + textLeft + textWidth, messageTop + message.y), context.color(ThemeColor::TextMuted), toast->message, TextAlign::Start);
        }

        const float close = toastCloseSize * scale;
        const ImRect closeArea(corner.x - toastPaddingRight * scale - close, top, corner.x - toastPaddingRight * scale, top + close);
        const ButtonState state = Widgets::interact(ImGui::GetID("##close"), closeArea);

        if (state.hovered) {
            list.AddRectFilled(closeArea.Min, closeArea.Max, Widgets::ink(context.color(ThemeColor::Hover)), context.metric(ThemeMetric::ControlRadius));
        }

        const float icon = toastCloseIcon * scale;
        IconCatalog::draw(context.fonts(), list, Icon::Close, ImFloor(ImVec2(closeArea.GetCenter().x - icon / 2.0F, closeArea.GetCenter().y - icon / 2.0F)), icon, context.color(ThemeColor::TextMuted));

        if (state.pressed && toast->dismissed < 0.0) {
            dismiss(*toast, now);
        }

        ImGui::End();
        ImGui::PopStyleVar();
        bottom -= height + spacing;

        // A fading toast keeps drawing every frame, and a resting one only asks for the frame that starts its fade.
        if (alpha < 1.0F) {
            context.requestFrame();
        } else {
            context.requestFrameAt(toast->shown + lifetimeSeconds);
        }
    }
}

std::size_t ToastOverlay::liveCount() const {
    // clang-format off
    return static_cast<std::size_t>(std::ranges::count_if(m_toasts, [](const Toast& toast) { return toast.dismissed < 0.0; }));
    // clang-format on
}

float ToastOverlay::opacity(const Toast& toast, double now) const {
    const double appearing = std::clamp((now - toast.shown) / fadeSeconds, 0.0, 1.0);
    const double leaving = toast.dismissed < 0.0 ? 1.0 : 1.0 - std::clamp((now - toast.dismissed) / fadeSeconds, 0.0, 1.0);
    const double eased = 1.0 - std::pow(1.0 - std::min(appearing, leaving), 3.0);
    return static_cast<float>(eased);
}

void ToastOverlay::dismiss(Toast& toast, double now) {
    toast.dismissed = now;
}

// A notification is recognised by its title and message while it has not been dismissed.
bool ToastOverlay::showing(std::string_view title, std::string_view message) const {
    // clang-format off
    return std::ranges::any_of(m_toasts, [&](const Toast& toast) { return toast.dismissed < 0.0 && toast.title == title && toast.message == message; });
    // clang-format on
}

} // namespace workpane::ui
