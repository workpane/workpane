#include "ui/shell/DialogHost.h"

#include "ui/TextFieldOptions.h"
#include "ui/TextFieldResult.h"
#include "ui/Widgets.h"
#include "ui/model/RenderContext.h"
#include "ui/model/Surface.h"
#include "ui/model/SurfaceStore.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

namespace workpane::ui {

void DialogHost::open(DialogRequest request) {
    ++m_opened;
    m_stack.push_back({std::move(request), false, {}, m_opened, std::nullopt});
}

void DialogHost::draw(RenderContext& context, SurfaceStore& surfaces, ImVec2 windowSize) {
    if (m_stack.empty()) {
        return;
    }

    drawLevel(context, surfaces, windowSize, 0);
    m_covered = GImGui->OpenPopupStack.Size > static_cast<int>(m_stack.size());

    // The window behind a dialog darkens over a few frames, which keep coming until it is fully dimmed.
    if (GImGui->DimBgRatio < 1.0F) {
        context.requestFrame();
    }
}

// A dialog carries a body when it says more than its title, asks for a value or shows the content of its plugin.
bool DialogHost::hasBody(const DialogRequest& request) {
    return !request.message.empty() || !request.detail.empty() || request.kind == DialogKind::Prompt || request.kind == DialogKind::Custom;
}

// The body stacks the message, the detail, the prompt field and the custom content, one spacing apart.
float DialogHost::bodyHeight(RenderContext& context, Surface* surface, const DialogRequest& request, float width) {
    const float spacing = dialogSpacing * context.scale();
    float height = 0.0F;

    for (const auto* text : {&request.message, &request.detail}) {
        if (!text->empty()) {
            height += (height > 0.0F ? spacing : 0.0F) + Widgets::paragraphSize(context, context.font(ThemeFont::Interface), *text, width).y;
        }
    }

    if (request.kind == DialogKind::Prompt) {
        height += (height > 0.0F ? spacing : 0.0F) + Widgets::controlHeight(context);
    }

    if (surface != nullptr) {
        context.setSurface(surface->id, surface->assets);
        height += (height > 0.0F ? spacing : 0.0F) + surface->root->measure(context, width).y;
    }

    return height;
}

// Each dialog is a modal popup inside the one below it, so only the dialog on top takes input and the ones below stay drawn behind it.
// A dialog takes the height its body needs when it opens, bounded by the window, and keeps it, so a problem it reports later or a page it switches to scrolls inside it instead of resizing it.
void DialogHost::drawLevel(RenderContext& context, SurfaceStore& surfaces, ImVec2 windowSize, std::size_t level) {
    Entry& entry = m_stack[level];
    const DialogRequest& request = entry.request;
    const bool top = level + 1 == m_stack.size();
    const float scale = context.scale();
    const float width = std::max(request.width, context.theme().metric(ThemeMetric::DialogMinimumWidth)) * scale;
    const float content = width - dialogHorizontalPadding * 2.0F * scale;
    const float spacing = dialogSpacing * scale;
    // A dialog takes the window of its level, since ImGui keeps every window it ever made, and the window starts afresh for each dialog that appears in it.
    const std::string name = "##dialog" + std::to_string(level);
    Surface* surface = request.kind == DialogKind::Custom ? surfaces.find(request.surface) : nullptr;
    const FontRole title = context.font(ThemeFont::SectionTitle);
    const float header = Widgets::paragraphSize(context, title, request.title, content).y;
    const auto actions = buttons(request);
    const float footer = actions.empty() ? 0.0F : spacing + dialogButtonGap * scale + Widgets::controlHeight(context);
    const float chrome = dialogVerticalPadding * 2.0F * scale + header + (hasBody(request) ? spacing : 0.0F) + footer;

    if (!ImGui::IsPopupOpen(name.c_str())) {
        ImGui::OpenPopup(name.c_str());
        entry.focusRequested = true;
    }

    if (!entry.height.has_value() && (request.kind != DialogKind::Custom || surface != nullptr)) {
        entry.height = chrome + bodyHeight(context, surface, request, content);
    }

    const float height = std::min(entry.height.value_or(chrome), std::max(chrome, windowSize.y - dialogMargin * 2.0F * scale));
    ImGui::SetNextWindowPos(ImVec2(windowSize.x / 2.0F, windowSize.y / 2.0F), ImGuiCond_Always, ImVec2(0.5F, 0.5F));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(dialogHorizontalPadding * scale, dialogVerticalPadding * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, dialogRadius * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0F, 0.0F));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, context.color(ThemeColor::Window).vector());

    if (!ImGui::BeginPopupModal(name.c_str(), nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
        return;
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    Widgets::paragraph(context, *ImGui::GetWindowDrawList(), title, ImRect(origin.x, origin.y, origin.x + content, origin.y + header), context.color(ThemeColor::Text), request.title, TextAlign::Start);
    float y = origin.y + header;
    std::string pressed;

    if (hasBody(request)) {
        const float visible = std::max(1.0F, height - chrome);
        ImGui::SetCursorScreenPos(ImVec2(origin.x, y + spacing));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));

        // A new dialog reads its body from the top, since the body window of its level may have scrolled for the dialog before it.
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetNextWindowScroll(ImVec2(0.0F, 0.0F));
        }

        // The body shares the keyboard navigation of the dialog, so Tab moves from its fields to the buttons under it.
        const bool shown = ImGui::BeginChild("##body", ImVec2(content, visible), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
        ImGui::PopStyleVar();

        // Every dialog gets an identity of its own inside the window of its level, so a field never inherits the text, selection or focus of a dialog closed before it.
        if (shown) {
            ImGui::PushID(static_cast<int>(entry.serial));
            pressed = drawBody(context, surface, entry, visible);
            ImGui::PopID();
        }

        ImGui::EndChild();
        y += spacing + visible;
    }

    // Buttons sit against the right edge in the order declared, and a button that does not close reports its press to the content of its dialog.
    float right = origin.x + content;
    y += actions.empty() ? 0.0F : spacing + dialogButtonGap * scale;

    for (auto action = actions.rbegin(); action != actions.rend(); ++action) {
        const ImVec2 size = Widgets::buttonSize(context, action->text, action->icon, action->variant);
        const ImRect bounds(right - size.x, y, right, y + size.y);
        ImGui::BeginDisabled(!action->enabled);
        const bool clicked = Widgets::button(context, "##" + action->id, bounds, action->text, action->icon, action->variant);
        ImGui::EndDisabled();
        right -= size.x + spacing;

        if (clicked && action->closes) {
            pressed = action->id;
        }

        if (clicked && !action->closes && surface != nullptr) {
            context.setSurface(surface->id, surface->assets);
            context.emit(surface->root->id(), "dialog-button", {{"button", action->id}});
        }
    }

    y += actions.empty() ? 0.0F : Widgets::controlHeight(context);
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(content, y - origin.y));

    // Escape closes a menu or a list opened inside the dialog first, which ImGui already did before the dialog is drawn, and mod+W closes the dialog like the window it stands for.
    if (top && pressed.empty() && !m_covered && (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_W))) {
        pressed = cancelButton;
    }

    // Return presses the safe choice of a destructive confirmation and the confirming choice of every other dialog.
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);

    if (top && pressed.empty() && enter && request.kind != DialogKind::Custom) {
        pressed = request.kind == DialogKind::Confirm && request.destructive ? cancelButton : confirmButton;
    }

    if (top && pressed.empty() && !entry.dismissal.empty()) {
        pressed = std::exchange(entry.dismissal, {});
    }

    // The answer of a closed dialog may open the next one, so nothing of this entry is read after it closes.
    if (top && !pressed.empty()) {
        close(level, pressed);
    }

    if (!top) {
        drawLevel(context, surfaces, windowSize, level + 1);
    }

    ImGui::EndPopup();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

// The body is drawn from the top of its scrolling region, custom content fills at least the height the dialog shows, and a body that scrolls keeps its content clear of the scroll bar by the spacing of the dialog.
std::string DialogHost::drawBody(RenderContext& context, Surface* surface, Entry& entry, float visible) {
    DialogRequest& request = entry.request;
    const float width = ImGui::GetContentRegionAvail().x - (ImGui::GetScrollMaxY() > 0.0F ? dialogSpacing * context.scale() : 0.0F);
    const float spacing = dialogSpacing * context.scale();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList& list = *ImGui::GetWindowDrawList();
    float y = origin.y;
    std::string pressed;

    for (const auto* text : {&request.message, &request.detail}) {
        if (text->empty()) {
            continue;
        }

        const FontRole font = context.font(ThemeFont::Interface);
        const ImVec2 size = Widgets::paragraphSize(context, font, *text, width);
        y += y > origin.y ? spacing : 0.0F;
        Widgets::paragraph(context, list, font, ImRect(origin.x, y, origin.x + width, y + size.y), context.color(text == &request.detail ? ThemeColor::TextMuted : ThemeColor::Text), *text, TextAlign::Start);
        y += size.y;
    }

    if (request.kind == DialogKind::Prompt) {
        y += y > origin.y ? spacing : 0.0F;
        const ImRect field(origin.x, y, origin.x + width, y + Widgets::controlHeight(context));
        const TextFieldOptions options{.placeholder = request.placeholder, .focus = entry.focusRequested, .selectAll = entry.focusRequested};
        const TextFieldResult result = Widgets::textField(context, "##prompt", field, request.value, options);
        entry.focusRequested = false;
        y = field.Max.y;
        pressed = result.submitted ? confirmButton : pressed;
    }

    if (surface != nullptr) {
        y += y > origin.y ? spacing : 0.0F;
        context.setSurface(surface->id, surface->assets);
        const ImVec2 size = surface->root->measure(context, width);
        const float height = std::max(size.y, visible - (y - origin.y));
        surface->root->draw(context, ImRect(origin.x, y, origin.x + width, y + height));
        y += height;
    }

    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(width, y - origin.y));

    return pressed;
}

bool DialogHost::active() const {
    return !m_stack.empty();
}

// Every open dialog is answered with its cancel button from the top down, which is what closing the product means for each of them.
void DialogHost::cancelAll() {
    while (!m_stack.empty()) {
        DialogRequest request = std::move(m_stack.back().request);
        m_stack.pop_back();

        if (request.answer) {
            request.answer({cancelButton, {}});
        }
    }
}

// A dialog closes inside its own popup once it is the one on top, so a dialog opened above it is answered first.
void DialogHost::dismiss(const std::string& surface, const std::string& button) {
    // clang-format off
    const auto found = std::ranges::find_if(m_stack, [&surface](const Entry& entry) { return entry.request.surface == surface; });
    // clang-format on

    if (found == m_stack.end()) {
        return;
    }

    found->dismissal = button;
}

// Every dialog of an owner that left is answered with its cancel button, each inside its own popup as a dismissal is.
void DialogHost::dismissOwner(const std::string& owner) {
    for (auto& entry : m_stack) {
        if (entry.request.owner == owner) {
            entry.dismissal = cancelButton;
        }
    }
}

void DialogHost::replaceButtons(const std::string& surface, std::vector<DialogButton> buttons) {
    // clang-format off
    const auto found = std::ranges::find_if(m_stack, [&surface](const Entry& entry) { return entry.request.surface == surface; });
    // clang-format on

    if (found != m_stack.end()) {
        found->request.buttons = std::move(buttons);
    }
}

// The request leaves the stack before its answer runs, so an answer that opens the next dialog finds a consistent stack.
void DialogHost::close(std::size_t level, const std::string& button) {
    ImGui::CloseCurrentPopup();
    DialogRequest request = std::move(m_stack[level].request);
    m_stack.erase(m_stack.begin() + static_cast<std::ptrdiff_t>(level));

    if (request.answer) {
        request.answer({button, request.value});
    }
}

DialogButton DialogHost::closing(std::string id, std::string text, ButtonVariant variant) {
    return {std::move(id), std::move(text), variant, std::nullopt, true, true};
}

std::vector<DialogButton> DialogHost::buttons(const DialogRequest& request) const {
    switch (request.kind) {
    case DialogKind::Confirm:
        return {closing(cancelButton, request.cancelText, ButtonVariant::Default), closing(confirmButton, request.confirmText, request.destructive ? ButtonVariant::Destructive : ButtonVariant::Primary)};
    case DialogKind::Alert:
        return {closing(confirmButton, request.confirmText, ButtonVariant::Primary)};
    case DialogKind::Prompt:
        return {closing(cancelButton, request.cancelText, ButtonVariant::Default), closing(confirmButton, request.confirmText, ButtonVariant::Primary)};
    case DialogKind::Custom:
        return request.buttons;
    }

    return {};
}

} // namespace workpane::ui
