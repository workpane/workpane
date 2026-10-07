#include "ui/model/Component.h"

#include "ui/Menus.h"
#include "ui/WidgetHelper.h"
#include "ui/model/RenderContext.h"
#include "ui/theme/ThemeColorNames.h"

#include <algorithm>
#include <string>
#include <utility>

namespace workpane::ui {

// A length is a number of logical pixels or the word auto, which hands the length back to the content.
void Component::readLength(json::ObjectReader& reader, std::string_view key, std::optional<float>& out, std::optional<Error>& failure, std::string_view kind) {
    if (!reader.contains(key)) {
        return;
    }

    const json::Json* value = &json::ObjectReader::absent();
    reader.readAny(key, value);

    if (value->is_string() && value->get<std::string>() == "auto") {
        out.reset();
        return;
    }

    if (!value->is_number() || value->get<double>() < 0.0 || value->get<double>() > largestLength) {
        if (!failure.has_value()) {
            failure = Error{"ui_length_invalid", "A length is neither a number in range nor auto", std::string(kind) + "." + std::string(key)};
        }

        return;
    }

    out = value->get<float>();
}

void Component::readBound(json::ObjectReader& reader, std::string_view key, float& out) {
    double value = out;
    reader.readNumber(key, value, 0.0, largestLength, json::Presence::Optional);
    out = static_cast<float>(value);
}

float Component::scaled(float value, float scale) {
    return value >= CommonProperties::unbounded ? CommonProperties::unbounded : value * scale;
}

Component::Component(NodeId id) : m_id(id) {}

NodeId Component::id() const {
    return m_id;
}

const CommonProperties& Component::common() const {
    return m_common;
}

// A column places a child across its width and a row across its height, each kind declares what it prefers on either axis, and a component given a width keeps it instead of stretching.
Alignment Component::columnAlignment() const {
    const Alignment preferred = m_common.width.has_value() && defaultColumnAlignment() == Alignment::Stretch ? Alignment::Start : defaultColumnAlignment();

    return m_common.align.value_or(preferred);
}

Alignment Component::rowAlignment() const {
    return m_common.align.value_or(defaultRowAlignment());
}

// The number of children a kind accepts, where zero marks a leaf.
std::size_t Component::childLimit() const {
    return 0;
}

std::vector<std::unique_ptr<Component>>& Component::children() {
    return m_children;
}

const std::vector<std::unique_ptr<Component>>& Component::children() const {
    return m_children;
}

// A node being built takes its first properties before its children, which are proven once they are attached.
Result<void> Component::apply(const json::Json& properties) {
    if (const auto proven = read(properties); !proven.hasValue()) {
        return proven;
    }

    applied();

    return Result<void>::success();
}

// A node on screen proves a patch against its children as well, and a refused patch puts every property it read back as it was.
Result<void> Component::patch(const json::Json& properties) {
    const CommonProperties common = m_common;
    const Restore restore = keep();
    auto proven = read(properties);

    if (proven.hasValue()) {
        proven = validateChildren();
    }

    if (!proven.hasValue()) {
        m_common = common;
        restore();
        return proven;
    }

    applied();

    return Result<void>::success();
}

Result<void> Component::validateChildren() const {
    return Result<void>::success();
}

ImVec2 Component::measure(RenderContext& context, float availableWidth) {
    if (!m_common.visible) {
        return {0.0F, 0.0F};
    }

    // A parent measures a child before drawing it and the child measures again while drawing, so one answer per frame and width is kept.
    if (m_measuredFrame == context.frame() && m_measuredWidth == availableWidth) {
        return m_measuredSize;
    }

    const float scale = context.scale();
    const float offered = m_common.width.has_value() ? *m_common.width * scale : boundedWidth(availableWidth, scale);
    const ImVec2 content = measureContent(context, offered);
    const float width = m_common.width.has_value() ? *m_common.width * scale : boundedWidth(content.x, scale);
    const float height = m_common.height.has_value() ? *m_common.height * scale : boundedHeight(content.y, scale);
    m_measuredFrame = context.frame();
    m_measuredWidth = availableWidth;
    m_measuredSize = ImVec2(width, height);

    return m_measuredSize;
}

// A size a parent assigns is still kept inside the bounds the node declares, scaled to the display.
float Component::boundedWidth(float width, float scale) const {
    const float minimum = m_common.minimumWidth * scale;
    return std::clamp(width, minimum, std::max(minimum, scaled(m_common.maximumWidth, scale)));
}

float Component::boundedHeight(float height, float scale) const {
    const float minimum = m_common.minimumHeight * scale;
    return std::clamp(height, minimum, std::max(minimum, scaled(m_common.maximumHeight, scale)));
}

// A node given less than a pixel draws nothing, because ImGui reads a size of zero or below as the rest of the window.
void Component::draw(RenderContext& context, const ImRect& bounds) {
    if (!m_common.visible || bounds.GetWidth() < 1.0F || bounds.GetHeight() < 1.0F) {
        return;
    }

    // A node outside the visible part of its window is laid out but not drawn, unless it takes the keyboard, brings itself into view or the keyboard moves through items, since all of them need it submitted.
    if (!ImGui::IsRectVisible(bounds.Min, bounds.Max) && !m_focusRequested && !revealing() && !GImGui->NavMoveScoringItems) {
        return;
    }

    // The identifier scope carries the bytes of the node identity, which Lua keeps unique inside its surface.
    const auto* identity = reinterpret_cast<const char*>(&m_id);
    ImGui::PushID(identity, identity + sizeof(m_id));

    if (!m_common.enabled) {
        ImGui::BeginDisabled();
    }

    if (m_focusRequested && focusMode() == FocusMode::Typing) {
        WidgetHelper::takeKeyboard();
    }

    render(context, bounds);

    if (m_focusRequested && focusMode() == FocusMode::Choosing) {
        WidgetHelper::takeNavigation();
    }

    m_focusRequested = false;

    if (!m_common.enabled) {
        ImGui::EndDisabled();
    }

    drawTooltip(context, bounds);
    drawMenu(context, bounds);
    drawDrag(context, bounds);
    drawDrop(context, bounds);
    ImGui::PopID();
}

// A kind that scrolls itself or one of its rows into view answers while it waits to, so it is drawn even outside the visible part of its window.
bool Component::revealing() const {
    return false;
}

// A command asks a component for something that is not a property, such as taking the focus or navigating a web view.
// A control that takes the keyboard answers focus by taking it the next time it is drawn.
Result<void> Component::command(RenderContext&, std::string_view name, const json::Json& arguments) {
    if (name != "focus" || focusMode() == FocusMode::None) {
        return Result<void>::failure({"ui_command_unknown", "A component was asked for a command it does not answer", std::string(kind()) + "." + std::string(name)});
    }

    json::ObjectReader reader(arguments, "component.focus");

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return finished;
    }

    m_focusRequested = true;

    return Result<void>::success();
}

FocusMode Component::focusMode() const {
    return FocusMode::None;
}

// Components holding native resources release them before the tree that owns them is dropped.
void Component::detach(RenderContext& context) {
    for (auto& child : m_children) {
        child->detach(context);
    }
}

// Work a component owns whether or not it is drawn, such as reading what a shell wrote while its view is elsewhere, happens here once per frame.
void Component::update(RenderContext&) {}

Alignment Component::defaultColumnAlignment() const {
    return Alignment::Stretch;
}

Alignment Component::defaultRowAlignment() const {
    return Alignment::Center;
}

Result<void> Component::validate() const {
    return Result<void>::success();
}

// A kind whose properties ask for work beyond keeping them, such as loading a text, does it once a patch is accepted.
void Component::applied() {}

Result<void> Component::read(const json::Json& properties) {
    m_readFailure.reset();
    m_measuredFrame = 0;
    json::ObjectReader reader(properties, std::string(kind()));
    readCommon(reader);
    readProperties(reader);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return finished;
    }

    if (m_readFailure.has_value()) {
        return Result<void>::failure(*m_readFailure);
    }

    return validate();
}

void Component::readText(json::ObjectReader& reader, std::string_view key, TextValue& out) {
    if (!reader.contains(key)) {
        return;
    }

    const json::Json* value = &json::ObjectReader::absent();
    reader.readAny(key, value);
    auto parsed = TextValue::parse(*value, std::string(kind()) + "." + std::string(key));

    if (!parsed.hasValue()) {
        fail(parsed.error());
        return;
    }

    out = std::move(parsed.value());
}

// Padding is one number for every side, two numbers for the vertical and horizontal sides, or four numbers from the top clockwise.
void Component::readInsets(json::ObjectReader& reader, std::string_view key, Insets& out) {
    if (!reader.contains(key)) {
        return;
    }

    const json::Json* value = &json::ObjectReader::absent();
    reader.readAny(key, value);
    std::vector<float> sides;

    if (value->is_number()) {
        sides.assign(4, value->get<float>());
    } else if (value->is_array() && (value->size() == 2 || value->size() == 4)) {
        for (const auto& side : *value) {
            sides.push_back(side.is_number() ? side.get<float>() : -1.0F);
        }
    }

    // clang-format off
    const bool valid = !sides.empty() && std::ranges::all_of(sides, [](float side) { return side >= 0.0F && side <= largestInset; });
    // clang-format on

    if (!valid) {
        fail({"ui_insets_invalid", "Insets are one, two or four numbers in range", std::string(kind()) + "." + std::string(key)});
        return;
    }

    out = sides.size() == 2 ? Insets{sides[0], sides[1], sides[0], sides[1]} : Insets{sides[0], sides[1], sides[2], sides[3]};
}

// A color is a theme role by name, and the word none clears it.
void Component::readColor(json::ObjectReader& reader, std::string_view key, std::optional<ThemeColor>& out) {
    if (!reader.contains(key)) {
        return;
    }

    std::string name;
    reader.read(key, name);

    if (name == "none") {
        out.reset();
        return;
    }

    const auto role = ThemeColorNames::parse(name);

    if (!role.has_value()) {
        fail({"ui_color_unknown", "A color names no theme role", std::string(kind()) + "." + std::string(key) + "=" + name});
        return;
    }

    out = role;
}

// An icon is named by the painted set, and the word none clears it.
void Component::readIcon(json::ObjectReader& reader, std::string_view key, std::optional<Icon>& out) {
    if (!reader.contains(key)) {
        return;
    }

    std::string name;
    reader.read(key, name);

    if (name == "none") {
        out.reset();
        return;
    }

    const auto icon = IconCatalog::parse(name);

    if (!icon.has_value()) {
        fail({"ui_icon_unknown", "An icon names nothing in the painted set", std::string(kind()) + "." + std::string(key) + "=" + name});
        return;
    }

    out = icon;
}

void Component::readSpacing(json::ObjectReader& reader, std::string_view key, float& out) {
    double value = out;
    reader.readNumber(key, value, 0.0, largestInset, json::Presence::Optional);
    out = static_cast<float>(value);
}

void Component::fail(Error error) {
    if (!m_readFailure.has_value()) {
        m_readFailure = std::move(error);
    }
}

void Component::readCommon(json::ObjectReader& reader) {
    reader.read("visible", m_common.visible, json::Presence::Optional).read("enabled", m_common.enabled, json::Presence::Optional);
    readText(reader, "tooltip", m_common.tooltip);

    double grow = m_common.grow;
    reader.readNumber("grow", grow, 0.0, largestGrow, json::Presence::Optional);
    m_common.grow = static_cast<float>(grow);

    std::int64_t collapse = m_common.collapse;
    reader.readInteger("collapse", collapse, 0, largestCollapse, json::Presence::Optional);
    m_common.collapse = static_cast<int>(collapse);

    readLength(reader, "width", m_common.width, m_readFailure, kind());
    readLength(reader, "height", m_common.height, m_readFailure, kind());
    readBound(reader, "minWidth", m_common.minimumWidth);
    readBound(reader, "maxWidth", m_common.maximumWidth);
    readBound(reader, "minHeight", m_common.minimumHeight);
    readBound(reader, "maxHeight", m_common.maximumHeight);

    if (reader.contains("align")) {
        Alignment align = Alignment::Stretch;
        reader.readChoice("align", align, {{"start", Alignment::Start}, {"center", Alignment::Center}, {"end", Alignment::End}, {"stretch", Alignment::Stretch}});
        m_common.align = align;
    }

    readDrag(reader);

    if (reader.contains("menu")) {
        const json::Json* items = &json::ObjectReader::absent();
        reader.readArray("menu", items);
        auto parsed = Menus::parse(*items, kind());

        if (!parsed.hasValue()) {
            fail(parsed.error());
            return;
        }

        m_common.menu = std::move(parsed.value());
    }
}

// A component offers a value to drag with a kind, and receives the values of the kinds it accepts.
void Component::readDrag(json::ObjectReader& reader) {
    const json::Json* drag = &json::ObjectReader::absent();
    reader.readObject("drag", drag, json::Presence::Optional).read("accepts", m_common.accepts, json::Presence::Optional);

    if (!std::ranges::all_of(m_common.accepts, DragValue::validKind)) {
        fail({"ui_drag_kind_invalid", "A drag kind is a dotted name of lowercase letters, numbers and hyphens", std::string(kind()) + ".accepts"});
    }

    if (!drag->is_object()) {
        return;
    }

    auto parsed = DragValue::parse(*drag, std::string(kind()) + ".drag");

    if (!parsed.hasValue()) {
        fail(parsed.error());
        return;
    }

    m_common.drag = std::move(parsed.value());
}

// A secondary click opens the menu of the innermost component under the pointer, which is drawn before the containers holding it.
void Component::drawMenu(RenderContext& context, const ImRect& bounds) {
    if (m_common.menu.empty() || !m_common.enabled) {
        return;
    }

    const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Right) && ImGui::IsMouseHoveringRect(bounds.Min, bounds.Max) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    if (clicked && context.claimMenuClick()) {
        ImGui::OpenPopup("##menu");
    }

    if (const auto picked = Menus::popup(context, "##menu", m_common.menu); picked.has_value()) {
        context.emit(id(), "menu", {{"item", *picked}});
    }
}

// A press starts a drag of the innermost draggable component under the pointer, which moves once the pointer travels past the drag threshold.
void Component::drawDrag(RenderContext& context, const ImRect& bounds) {
    if (!m_common.drag.has_value() || !m_common.enabled) {
        return;
    }

    const bool pressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(bounds.Min, bounds.Max) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if (pressed) {
        context.pressDrag(id(), *m_common.drag);
        return;
    }

    // The control that took the press lets it go once the drag moves, so releasing the pointer never also clicks it.
    if (context.dragPressed(id()) && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        context.moveDrag();
        ImGui::ClearActiveID();
    }
}

// The innermost component under the pointer that accepts the dragged kind is outlined, and it receives the value when the pointer lets go over it.
void Component::drawDrop(RenderContext& context, const ImRect& bounds) {
    const DragValue* dragged = context.dragging();

    if (dragged == nullptr || !m_common.enabled || context.dragSource(id()) || std::ranges::find(m_common.accepts, dragged->kind) == m_common.accepts.end()) {
        return;
    }

    const bool hovered = ImGui::IsMouseHoveringRect(bounds.Min, bounds.Max) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if (!hovered || !context.claimDropTarget()) {
        return;
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        context.emit(id(), "drop", {{"kind", dragged->kind}, {"value", dragged->value}});
        return;
    }

    WidgetHelper::focusBorder(context, bounds);
}

void Component::drawTooltip(RenderContext& context, const ImRect& bounds) {
    if (m_common.tooltip.empty()) {
        return;
    }

    // The hover is read from the window, which a disabled component does not change, so a disabled component still explains itself.
    // A pointer resting while the keyboard navigates explains nothing, as ImGui ignores the pointer until it moves again.
    const bool hovered = !GImGui->NavHighlightItemUnderNav && ImGui::IsMouseHoveringRect(bounds.Min, bounds.Max) && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if (!hovered) {
        m_hoverStarted = -1.0;
        return;
    }

    if (m_hoverStarted < 0.0) {
        m_hoverStarted = context.time();
    }

    // The tooltip waits the conventional delay, and the frame that shows it is asked for because nothing else would draw it.
    if (context.time() - m_hoverStarted < tooltipDelaySeconds) {
        context.requestFrameAt(m_hoverStarted + tooltipDelaySeconds);
        return;
    }

    WidgetHelper::tooltip(context, context.text(m_common.tooltip));
}

std::vector<Component*> Component::visibleChildren() const {
    std::vector<Component*> visible;

    for (const auto& child : children()) {
        if (child->common().visible) {
            visible.push_back(child.get());
        }
    }

    return visible;
}

// Children start on whole pixels, so text inside them is never resampled between two columns of the display.
ImRect Component::snapped(float x, float y, float width, float height) {
    const ImVec2 origin(std::floor(x), std::floor(y));
    return {origin, ImVec2(origin.x + std::max(0.0F, width), origin.y + std::max(0.0F, height))};
}

float Component::alignedOffset(Alignment alignment, float available, float size) {
    switch (alignment) {
    case Alignment::Center:
        return (available - size) / 2.0F;
    case Alignment::End:
        return available - size;
    case Alignment::Start:
    case Alignment::Stretch:
        return 0.0F;
    }

    return 0.0F;
}

} // namespace workpane::ui
