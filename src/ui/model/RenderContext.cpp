#include "ui/model/RenderContext.h"

#include "localization/Localization.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

namespace workpane::ui {

RenderContext::RenderContext(const Theme& theme, const Fonts& fonts, FontFamilies& families, const localization::Localization& localization, TextureCache& textures, EventSink& events, NativeViewHost& nativeViews, PseudoTerminalHost& terminals, float scale) : m_theme(&theme), m_fonts(fonts), m_families(families), m_localization(localization), m_textures(textures), m_events(events), m_nativeViews(nativeViews), m_terminals(terminals), m_scale(scale) {}

const Theme& RenderContext::theme() const {
    return *m_theme;
}

const Fonts& RenderContext::fonts() const {
    return m_fonts;
}

FontFamilies& RenderContext::families() const {
    return m_families;
}

const localization::Localization& RenderContext::localization() const {
    return m_localization;
}

TextureCache& RenderContext::textures() const {
    return m_textures;
}

NativeViewHost& RenderContext::nativeViews() const {
    return m_nativeViews;
}

PseudoTerminalHost& RenderContext::terminals() const {
    return m_terminals;
}

float RenderContext::scale() const {
    return m_scale;
}

float RenderContext::metric(ThemeMetric role) const {
    return m_theme->metric(role) * m_scale;
}

Color RenderContext::color(ThemeColor role) const {
    return m_theme->color(role);
}

FontRole RenderContext::font(ThemeFont role) const {
    return m_theme->font(role);
}

const std::string& RenderContext::text(const TextValue& value) const {
    return value.resolve(m_localization);
}

std::string RenderContext::translate(std::string_view key) const {
    return m_localization.translate(key);
}

void RenderContext::setTheme(const Theme& theme) {
    m_theme = &theme;
}

void RenderContext::setScale(float scale) {
    m_scale = scale;
}

void RenderContext::setSurface(std::string surface, std::filesystem::path assets) {
    m_surface = std::move(surface);
    m_assets = std::move(assets);
}

const std::string& RenderContext::surface() const {
    return m_surface;
}

const std::filesystem::path& RenderContext::assets() const {
    return m_assets;
}

void RenderContext::emit(NodeId node, std::string name, nlohmann::json value) {
    m_events.emit({m_surface, node, std::move(name), std::move(value), nlohmann::json::object(), {}});
    m_frameRequested = true;
}

// An event that changes what its node shows names each property it changed with the field of its value that holds it, which the node on the Lua side takes as well.
void RenderContext::emit(NodeId node, std::string name, nlohmann::json value, nlohmann::json state, std::vector<NodeId> order) {
    m_events.emit({m_surface, node, std::move(name), std::move(value), std::move(state), std::move(order)});
    m_frameRequested = true;
}

// An event that follows the clock, such as the tick of a canvas, asks for no frame of its own, because what its plugin answers wakes the loop.
void RenderContext::report(NodeId node, std::string name, nlohmann::json value) {
    m_events.emit({m_surface, node, std::move(name), std::move(value), nlohmann::json::object(), {}});
}

// Every frame carries its own number, which is what a measurement cached during it is keyed by.
// Files that no component took in the frame that followed their drop are gone, so a later frame never delivers them somewhere else.
// A drag ends in the frame after the pointer lets go, which is the frame its target received it in, and escape abandons it.
void RenderContext::beginFrame(double seconds) {
    m_time = seconds;
    m_paragraphs.clear();
    m_menuClaimed = false;
    m_activationClaimed = false;
    ++m_frame;

    if (m_droppedFiles.has_value() && m_frame > m_filesFrame) {
        m_droppedFiles.reset();
    }

    // A window a page opened and no component adopted within a few frames is closed, which the page sees as its window closing.
    // clang-format off
    std::erase_if(m_popups, [this](const auto& entry) { return m_frame > entry.second.frame + popupFrames; });
    // clang-format on

    const bool ended = !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseReleased(ImGuiMouseButton_Left);

    if (m_drag.has_value() && (ended || ImGui::IsKeyPressed(ImGuiKey_Escape))) {
        m_drag.reset();
    }
}

// Files dropped on the window wait for the frame that follows, where the first component under the pointer that asks for them takes them.
void RenderContext::dropFiles(std::vector<std::filesystem::path> paths, ImVec2 position) {
    m_droppedFiles = std::move(paths);
    m_filesPosition = position;
    m_filesFrame = m_frame + 1;
    m_frameRequested = true;
}

std::optional<std::vector<std::filesystem::path>> RenderContext::takeDroppedFiles(const ImRect& bounds) {
    if (!m_droppedFiles.has_value() || !bounds.Contains(m_filesPosition)) {
        return std::nullopt;
    }

    return std::exchange(m_droppedFiles, std::nullopt);
}

// The innermost draggable component is drawn first, so the first press in a frame is the one that holds the drag.
void RenderContext::pressDrag(NodeId node, const DragValue& value) {
    if (m_drag.has_value() && m_drag->frame == m_frame) {
        return;
    }

    m_drag = DragSession{m_surface, node, value, m_frame, false};
}

bool RenderContext::dragPressed(NodeId node) const {
    return m_drag.has_value() && !m_drag->moving && dragSource(node);
}

bool RenderContext::dragSource(NodeId node) const {
    return m_drag.has_value() && m_drag->source == node && m_drag->surface == m_surface;
}

void RenderContext::moveDrag() {
    if (m_drag.has_value()) {
        m_drag->moving = true;
    }
}

// A drag that has moved is what targets are offered, and a press that never moved offers nothing.
const DragValue* RenderContext::dragging() const {
    return m_drag.has_value() && m_drag->moving ? &m_drag->value : nullptr;
}

// Nested targets are drawn from the innermost out, so the first one under the pointer to claim a frame is the one that receives the drop.
bool RenderContext::claimDropTarget() {
    if (m_dropClaimFrame == m_frame) {
        return false;
    }

    m_dropClaimFrame = m_frame;

    return true;
}

// One secondary click opens one menu, so the first component to claim it in a frame is the only one that does.
bool RenderContext::claimMenuClick() {
    if (m_menuClaimed) {
        return false;
    }

    m_menuClaimed = true;

    return true;
}

// Nested components are drawn from the inside out, so the innermost one that asks for a double click is the one that answers it.
bool RenderContext::claimActivation() {
    if (m_activationClaimed) {
        return false;
    }

    m_activationClaimed = true;

    return true;
}

std::uint64_t RenderContext::frame() const {
    return m_frame;
}

double RenderContext::time() const {
    return m_time;
}

// Something that moves asks for the next frame, so the scheduler keeps drawing only while anything is animating.
void RenderContext::requestFrame() {
    m_frameRequested = true;
}

// Something that changes at a known moment asks for a frame then, so the loop sleeps until that moment instead of drawing every frame.
void RenderContext::requestFrameAt(double seconds) {
    m_frameDeadline = std::min(m_frameDeadline, seconds);
}

bool RenderContext::frameRequested() const {
    return m_frameRequested;
}

double RenderContext::frameDeadline() const {
    return m_frameDeadline;
}

void RenderContext::resetFrameRequest() {
    m_frameRequested = false;
    m_frameDeadline = std::numeric_limits<double>::infinity();
}

void RenderContext::setModalActive(bool active) {
    m_modalActive = active;
}

bool RenderContext::modalActive() const {
    return m_modalActive;
}

// A window a page opened waits here under a number its owner hands to the component that will show it.
std::uint64_t RenderContext::keepPopup(std::unique_ptr<NativeWebView> popup) {
    m_popups.emplace(++m_nextPopup, KeptPopup{std::move(popup), m_frame});

    return m_nextPopup;
}

std::unique_ptr<NativeWebView> RenderContext::takePopup(std::uint64_t popup) {
    const auto found = m_popups.find(popup);

    if (found == m_popups.end()) {
        return nullptr;
    }

    auto view = std::move(found->second.view);
    m_popups.erase(found);

    return view;
}

// The text an input method is composing is kept until it is committed or abandoned, with the byte offset of its caret.
void RenderContext::compose(std::string text, std::size_t caret) {
    m_composition = std::move(text);
    m_compositionCaret = std::min(caret, m_composition.size());
}

const std::string& RenderContext::composition() const {
    return m_composition;
}

std::size_t RenderContext::compositionCaret() const {
    return m_compositionCaret;
}

// Paragraphs broken during this frame, which measuring and drawing share.
ParagraphCache& RenderContext::paragraphs() const {
    return m_paragraphs;
}

} // namespace workpane::ui
