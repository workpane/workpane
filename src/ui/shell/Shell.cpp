#include "ui/shell/Shell.h"

#include "ui/IconCatalog.h"
#include "ui/Painter.h"
#include "ui/Widgets.h"
#include "ui/model/DragValue.h"
#include "ui/model/RenderContext.h"
#include "ui/model/Surface.h"
#include "ui/model/SurfaceStore.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

namespace workpane::ui {

Shell::Shell(ViewRequest viewRequest, BandRequest bandRequest, SettingsView::SectionRequest sectionRequest, QuitRequest quitRequest, ShortcutRequest shortcutRequest) : m_viewRequest(std::move(viewRequest)), m_bandRequest(std::move(bandRequest)), m_sectionRequest(std::move(sectionRequest)), m_quitRequest(std::move(quitRequest)), m_shortcutRequest(std::move(shortcutRequest)) {}

// Primary destinations come first and secondary ones after them, each in the position its plugin declared.
void Shell::setNavigation(std::vector<NavigationItem> items) {
    // clang-format off
    std::ranges::stable_sort(items, [](const NavigationItem& left, const NavigationItem& right) { return left.placement != right.placement ? left.placement == NavigationPlacement::Primary : left.order < right.order; });
    // clang-format on
    m_navigation = std::move(items);

    // clang-format off
    const bool known = m_destination == settingsDestination || std::ranges::any_of(m_navigation, [this](const NavigationItem& item) { return item.destination() == m_destination; });
    // clang-format on

    if (!known) {
        m_destination = m_navigation.empty() ? std::string(settingsDestination) : m_navigation.front().destination();
    }

    forgetVanishedSurfaces();
    requestPreloads();
}

// Bands across the top come before the ones across the bottom, each in the position its plugin declared.
void Shell::setBands(std::vector<BandItem> bands) {
    // clang-format off
    std::ranges::stable_sort(bands, [](const BandItem& left, const BandItem& right) { return left.placement != right.placement ? left.placement == BandPlacement::Top : left.order < right.order; });
    // clang-format on
    m_bands = std::move(bands);
    forgetVanishedSurfaces();
}

void Shell::setSettingsGroups(std::vector<SettingsGroup> groups) {
    m_settings.setGroups(std::move(groups));
}

// The shell opens on the first destination the bar shows once every plugin has declared its own, and builds at once the views that asked to run before they are shown.
void Shell::setReady(bool ready) {
    m_ready = ready;

    if (ready && !m_navigation.empty()) {
        m_destination = m_navigation.front().destination();
    }

    requestPreloads();
}

// A view that asked to run before it is shown is built as soon as the workspace is ready, or at once when its plugin starts later.
void Shell::requestPreloads() {
    for (const NavigationItem& item : m_navigation) {
        if (m_ready && item.preload && m_requested.insert(item.surface()).second) {
            m_viewRequest(item);
        }
    }
}

// The requests and failures of surfaces whose plugin left are forgotten, so the plugin builds them again when it starts once more.
void Shell::forgetVanishedSurfaces() {
    std::set<std::string, std::less<>> present;

    for (const auto& item : m_navigation) {
        present.insert(item.surface());
    }

    for (const auto& band : m_bands) {
        present.insert(band.surface());
    }

    // clang-format off
    std::erase_if(m_requested, [&present](const std::string& surface) { return !present.contains(surface); });
    std::erase_if(m_failed, [&present](const std::string& surface) { return !present.contains(surface); });
    // clang-format on
}

bool Shell::ready() const {
    return m_ready;
}

Result<void> Shell::navigate(const std::string& destination) {
    // clang-format off
    const bool known = destination == settingsDestination || std::ranges::any_of(m_navigation, [&destination](const NavigationItem& item) { return item.destination() == destination; });
    // clang-format on

    if (!known) {
        return Result<void>::failure({"shell_destination_unknown", "No destination is declared under that identity", destination});
    }

    m_destination = destination;

    return Result<void>::success();
}

const std::string& Shell::destination() const {
    return m_destination;
}

const std::vector<NavigationItem>& Shell::navigation() const {
    return m_navigation;
}

const std::vector<BandItem>& Shell::bands() const {
    return m_bands;
}

void Shell::markFailed(const std::string& surface) {
    m_failed.insert(surface);
    m_settings.markFailed(surface);
}

ToastOverlay& Shell::toasts() {
    return m_toasts;
}

DialogHost& Shell::dialogs() {
    return m_dialogs;
}

SettingsView& Shell::settings() {
    return m_settings;
}

void Shell::draw(RenderContext& context, SurfaceStore& surfaces, ImVec2 windowSize) {
    context.setModalActive(m_dialogs.active());
    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
    ImGui::SetNextWindowSize(windowSize);
    ImGui::Begin(windowName, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);

    if (!m_ready) {
        drawLoading(context, windowSize);
    } else {
        const ImRect middle = drawBands(context, surfaces, windowSize);
        const auto modes = entries(context);
        const float barWidth = m_modeBar.width(context, modes);

        if (const auto picked = m_modeBar.draw(context, ImRect(0.0F, middle.Min.y, barWidth, middle.Max.y), modes, m_destination); picked.has_value()) {
            m_destination = *picked;
        }

        Painter::verticalDivider(*ImGui::GetWindowDrawList(), ImVec2(barWidth, middle.Min.y), middle.GetHeight(), context.metric(ThemeMetric::LineWidth), context.color(ThemeColor::Border));
        const ImRect content(barWidth + context.metric(ThemeMetric::LineWidth), middle.Min.y, windowSize.x, middle.Max.y);

        if (m_destination == settingsDestination) {
            m_settings.draw(context, surfaces, content, m_sectionRequest);
        }

        for (const auto& item : m_navigation) {
            if (item.destination() == m_destination) {
                // clang-format off
                drawSurface(context, surfaces, content, item.surface(), [this, &item]() { m_viewRequest(item); });
                // clang-format on
            }
        }

        handleShortcuts(modes);
        m_contentBottom = middle.Max.y;
    }

    ImGui::End();

    // Notices wait for the workspace, so none is spent over the loading screen, and they stand above the bands across the bottom.
    if (m_ready) {
        m_toasts.draw(context, windowSize.x, m_contentBottom);
    }

    m_dialogs.draw(context, surfaces, windowSize);
    drawDrag(context);
    drawComposition(context);
    releaseVanishedFocus();

    if (settling()) {
        context.requestFrame();
    }
}

// The windows drawn above the views, such as tooltips, menus, lists and notifications, whose rectangles a native view leaves uncovered.
std::vector<ImRect> Shell::floatingWindows() {
    std::vector<ImRect> rectangles;
    const ImGuiWindow* main = ImGui::FindWindowByName(windowName);

    for (const ImGuiWindow* window : GImGui->Windows) {
        const bool floating = (window->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Modal)) == 0;

        if (window->Active && !window->Hidden && floating && window != main) {
            rectangles.push_back(window->Rect());
        }
    }

    return rectangles;
}

// A window that appears, such as a tooltip or a menu, is measured while hidden and reaches its size in the frames after, so frames keep coming until every window has settled.
bool Shell::settling() {
    for (ImGuiWindow* window : GImGui->Windows) {
        if (!window->Active) {
            continue;
        }

        const ImVec2 fitted = ImGui::CalcWindowNextAutoFitSize(window);
        const bool hidden = window->HiddenFramesCanSkipItems > 0 || window->HiddenFramesCannotSkipItems > 0;
        const bool growing = (window->AutoFitFramesX > 0 && std::abs(window->Size.x - fitted.x) > settledTolerance) || (window->AutoFitFramesY > 0 && std::abs(window->Size.y - fitted.y) > settledTolerance);

        if (hidden || growing) {
            return true;
        }
    }

    return false;
}

// A control that vanished while it held the keyboard, such as a field its form hid, lets it go, so the next Tab starts again from the first control.
void Shell::releaseVanishedFocus() {
    const ImGuiContext& g = *GImGui;

    if (g.NavId == 0 || g.NavIdIsAlive || g.NavWindow == nullptr) {
        return;
    }

    ImGui::SetNavID(0, g.NavLayer, g.NavFocusScopeId, ImRect());
    ImGui::SetNavCursorVisible(false);
}

// A drag that has moved carries its label beside the pointer, above everything the product draws.
void Shell::drawDrag(const RenderContext& context) {
    const DragValue* dragged = context.dragging();

    if (dragged == nullptr || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        return;
    }

    ImDrawList& list = *ImGui::GetForegroundDrawList();
    const FontRole font = context.font(ThemeFont::Interface);
    const std::string& label = context.text(dragged->label);
    const float padding = context.metric(ThemeMetric::ControlHorizontalPadding);
    const float radius = context.metric(ThemeMetric::ControlRadius);
    const ImVec2 origin = ImGui::GetIO().MousePos + ImVec2(context.metric(ThemeMetric::ItemSpacing), context.metric(ThemeMetric::ItemSpacing));
    const ImRect box(ImFloor(origin), ImFloor(origin) + ImVec2(Widgets::textSize(context, font, label).x + padding * 2.0F, context.metric(ThemeMetric::ControlHeight)));

    list.AddRectFilled(box.Min, box.Max, Widgets::ink(context.color(ThemeColor::Raised)), radius);
    Painter::rectBorder(list, box.Min, box.Max, radius, context.metric(ThemeMetric::LineWidth), context.color(ThemeColor::Accent));
    Widgets::alignedText(context, list, font, box, context.color(ThemeColor::Text), label, TextAlign::Center);
}

// The text an input method composes is drawn over the caret of the focused text, underlined in the accent and with a caret of its own, since only the input methods of Windows and X11 draw it themselves.
void Shell::drawComposition(const RenderContext& context) {
    const ImGuiPlatformImeData& caret = GImGui->PlatformImeData;
    const std::string& text = context.composition();

    if (text.empty() || !caret.WantVisible) {
        return;
    }

    ImDrawList& list = *ImGui::GetForegroundDrawList();
    const FontRole font = context.font(ThemeFont::Interface);
    const float line = std::max(caret.InputLineHeight, Widgets::lineHeight(context, font));
    const ImVec2 origin = ImFloor(caret.InputPos + ImVec2(1.0F, (caret.InputLineHeight - line) / 2.0F));
    const float before = Widgets::textSize(context, font, text.substr(0, context.compositionCaret())).x;
    const float stroke = context.scale();
    const ImRect box(origin, origin + ImVec2(Widgets::textSize(context, font, text).x, line));

    list.AddRectFilled(box.Min, box.Max, Widgets::ink(context.color(ThemeColor::Raised)));
    Widgets::alignedText(context, list, font, box, context.color(ThemeColor::Text), text, TextAlign::Start);
    list.AddLine(ImVec2(box.Min.x, box.Max.y - stroke), ImVec2(box.Max.x, box.Max.y - stroke), Widgets::ink(context.color(ThemeColor::Accent)), stroke);
    list.AddLine(ImVec2(box.Min.x + before, box.Min.y), ImVec2(box.Min.x + before, box.Max.y), Widgets::ink(context.color(ThemeColor::Text)), stroke);
}

// A window that is still loading says so in the middle of itself, with the indicator turning above the sentence.
void Shell::drawLoading(RenderContext& context, ImVec2 windowSize) {
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const FontRole font = context.font(ThemeFont::Interface);
    const float indicator = loadingIndicatorSize * context.scale();
    const float spacing = context.metric(ThemeMetric::ControlVerticalPadding);
    const float line = Widgets::lineHeight(context, font);
    const float top = (windowSize.y - indicator - spacing - line) / 2.0F;

    Painter::busyRing(list, ImVec2(windowSize.x / 2.0F, top + indicator / 2.0F), indicator / 2.0F - 2.0F * context.scale(), context.time(), context.color(ThemeColor::Accent));
    Widgets::alignedText(context, list, font, ImRect(0.0F, top + indicator + spacing, windowSize.x, top + indicator + spacing + line), context.color(ThemeColor::TextMuted), context.translate("workpane.window.loading"), TextAlign::Center);
    context.requestFrame();
}

// The bands take their height across the whole width, the ones of the top from the top edge down and the ones of the bottom from the bottom edge up, each with one divider toward the middle, and the middle is what they leave.
ImRect Shell::drawBands(RenderContext& context, SurfaceStore& surfaces, ImVec2 windowSize) {
    const float line = context.metric(ThemeMetric::LineWidth);
    const Color border = context.color(ThemeColor::Border);
    ImDrawList& list = *ImGui::GetWindowDrawList();
    float top = 0.0F;
    float bottom = windowSize.y;

    for (const auto& band : m_bands) {
        if (band.height > 0 && band.placement == BandPlacement::Bottom) {
            bottom -= static_cast<float>(band.height) * context.scale() + line;
        }
    }

    float next = bottom;

    for (const auto& band : m_bands) {
        const float height = static_cast<float>(band.height) * context.scale();

        if (band.height <= 0) {
            continue;
        }

        const bool atTop = band.placement == BandPlacement::Top;
        const ImRect bounds = atTop ? ImRect(0.0F, top, windowSize.x, top + height) : ImRect(0.0F, next + line, windowSize.x, next + line + height);
        Painter::horizontalDivider(list, ImVec2(0.0F, atTop ? bounds.Max.y : next), windowSize.x, line, border);
        // clang-format off
        drawSurface(context, surfaces, bounds, band.surface(), [this, &band]() { m_bandRequest(band); });
        // clang-format on

        if (atTop) {
            top = bounds.Max.y + line;
        } else {
            next = bounds.Max.y;
        }
    }

    return {0.0F, top, windowSize.x, std::max(top, bottom)};
}

// Draws a mounted surface inside a child window of its own, which clips it and scopes the identities of its controls, the failure message of one its plugin could not build, or asks once for one nobody built yet.
void Shell::drawSurface(RenderContext& context, SurfaceStore& surfaces, const ImRect& bounds, const std::string& surfaceId, const std::function<void()>& request) {
    Surface* surface = surfaces.find(surfaceId);

    if (surface == nullptr) {
        if (m_failed.contains(surfaceId)) {
            ImDrawList& list = *ImGui::GetWindowDrawList();
            const FontRole font = context.font(ThemeFont::Interface);
            const std::string message = context.translate("workpane.view.failed-message");
            const ImVec2 size = Widgets::paragraphSize(context, font, message, bounds.GetWidth());
            Widgets::paragraph(context, list, font, ImRect(bounds.Min.x, bounds.GetCenter().y - size.y / 2.0F, bounds.Max.x, bounds.Max.y), context.color(ThemeColor::TextMuted), message, TextAlign::Center);
        } else if (m_requested.insert(surfaceId).second) {
            request();
        }

        return;
    }

    context.setSurface(surface->id, surface->assets);
    ImGui::SetCursorScreenPos(bounds.Min);

    if (ImGui::BeginChild(surfaceId.c_str(), bounds.GetSize(), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        surface->root->draw(context, bounds);
    }

    ImGui::EndChild();
}

void Shell::handleShortcuts(const std::vector<ModeEntry>& entries) {
    if (m_dialogs.active()) {
        return;
    }

    if (corePressed(ImGuiMod_Ctrl | ImGuiKey_Q)) {
        m_quitRequest();
    }

    if (corePressed(ImGuiMod_Ctrl | ImGuiKey_Comma)) {
        m_destination = settingsDestination;
    }

    if (m_destination == settingsDestination && corePressed(ImGuiMod_Ctrl | ImGuiKey_F)) {
        m_settings.focusSearch();
    }

    // The number keys reach the destinations in the order the bar shows them, from the top down.
    const std::array<ImGuiKey, 9> digits{ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4, ImGuiKey_5, ImGuiKey_6, ImGuiKey_7, ImGuiKey_8, ImGuiKey_9};

    for (std::size_t index = 0; index < digits.size() && index < entries.size(); ++index) {
        if (corePressed(ImGuiMod_Ctrl | digits[index])) {
            m_destination = entries[index].destination;
        }
    }

    // A plugin shortcut belongs to the view that declared it, so only the shortcuts of the view on screen are answered, even while one of its fields has the keyboard.
    for (const auto& item : m_navigation) {
        if (item.destination() != m_destination) {
            continue;
        }

        for (const auto& shortcut : item.shortcuts) {
            if (ImGui::Shortcut(shortcut.chord, ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_RouteOverActive)) {
                m_shortcutRequest(item, shortcut.id);
            }
        }
    }
}

// A core combination is routed over the item that has the keyboard, because a route follows what the frame before asked for, and it is answered unless that item keeps every key in this frame, as a terminal does with the combinations it sends to its shell.
bool Shell::corePressed(ImGuiKeyChord chord) {
    return ImGui::Shortcut(chord, ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_RouteOverActive) && !GImGui->ActiveIdUsingAllKeyboardKeys;
}

// The Settings destination is appended after every plugin destination and is always the last one of the bar.
std::vector<ModeEntry> Shell::entries(RenderContext& context) const {
    std::vector<ModeEntry> modes;

    for (const auto& item : m_navigation) {
        modes.push_back({item.destination(), context.translate(item.titleKey), item.icon, item.placement});
    }

    modes.push_back({settingsDestination, context.translate("workpane.navigation.settings"), Icon::Settings, NavigationPlacement::Secondary});

    return modes;
}

} // namespace workpane::ui
