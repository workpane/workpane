#include "ui/components/views/Canvas.h"

#include "localization/Localization.h"
#include "ui/AssetPath.h"
#include "ui/Texture.h"
#include "ui/TextureCache.h"
#include "ui/WheelScale.h"
#include "ui/shell/KeyChords.h"
#include "ui/theme/ThemeColorNames.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>
#include <utility>

namespace workpane::ui {

Canvas::Canvas(NodeId id) : Component(id) {}

std::string_view Canvas::kind() const {
    return "canvas";
}

Alignment Canvas::defaultRowAlignment() const {
    return Alignment::Stretch;
}

void Canvas::readProperties(json::ObjectReader& reader) {
    reader.readInteger("frameRate", m_frameRate, 0, fastestRate, json::Presence::Optional).read("pixelated", m_pixelated, json::Presence::Optional).read("focusable", m_focusable, json::Presence::Optional).read("tracking", m_tracking, json::Presence::Optional);

    if (reader.contains("pictures")) {
        reader.read("pictures", m_pictures);
        m_picturesChanged = true;
    }

    if (!reader.contains("background")) {
        return;
    }

    std::string background;
    reader.readText("background", background);

    if (background == "none") {
        m_background.reset();
        return;
    }

    auto parsed = paint(background, 1.0, "canvas.background");

    if (!parsed.hasValue()) {
        fail(parsed.error());
        return;
    }

    m_background = parsed.value();
}

Component::Restore Canvas::keep() {
    return kept(m_frameRate, m_pixelated, m_focusable, m_tracking, m_background, m_pictures);
}

// The pictures a canvas keeps ready are plain paths inside the assets of its plugin, and a bounded list of them.
Result<void> Canvas::validate() const {
    if (m_pictures.size() > mostPictures) {
        return Result<void>::failure({"ui_canvas_pictures_too_many", "A canvas keeps at most two hundred and fifty-six pictures ready", std::to_string(m_pictures.size())});
    }

    for (std::size_t index = 0; index < m_pictures.size(); ++index) {
        if (!AssetPath::safe(m_pictures[index])) {
            return Result<void>::failure({"ui_canvas_picture_invalid", "A picture a canvas keeps ready is a plain path inside the assets of its plugin", "canvas.pictures[" + std::to_string(index) + "]"});
        }
    }

    return Component::validate();
}

// Holds the pictures its plugin names from the frame it mounts, so each one is decoded before the first frame that draws it and stays while the canvas does, and lets go of the ones its plugin no longer names.
void Canvas::update(RenderContext& context) {
    if (!m_picturesChanged) {
        return;
    }

    std::vector<std::filesystem::path> wanted;
    wanted.reserve(m_pictures.size());

    for (const auto& picture : m_pictures) {
        wanted.push_back(context.assets() / picture);
    }

    std::ranges::sort(wanted);
    wanted.erase(std::ranges::unique(wanted).begin(), wanted.end());

    for (const auto& path : wanted) {
        if (!std::ranges::binary_search(m_holding, path)) {
            context.textures().hold(path);
        }
    }

    for (const auto& path : m_holding) {
        if (!std::ranges::binary_search(wanted, path)) {
            context.textures().drop(path);
        }
    }

    m_holding = std::move(wanted);
    m_picturesChanged = false;
}

// Lets go of every picture it held, so the pictures of a plugin that left or turned off cost nothing.
void Canvas::detach(RenderContext& context) {
    for (const auto& path : m_holding) {
        context.textures().drop(path);
    }

    m_holding.clear();
    Component::detach(context);
}

// A canvas takes the width it is given and stands a fixed height until its plugin gives it one or lets it grow.
ImVec2 Canvas::measureContent(RenderContext& context, float availableWidth) {
    return {availableWidth, defaultHeight * context.scale()};
}

// Draws the list its plugin sent, then reports the pointer, the keys and the tick of this frame, so what the plugin answers is drawn in the next one.
void Canvas::render(RenderContext& context, const ImRect& bounds) {
    const ImGuiID item = ImGui::GetID("##canvas");
    ImGui::SetCursorScreenPos(bounds.Min);
    ImGui::ItemSize(bounds.GetSize());
    std::ignore = ImGui::ItemAdd(bounds, item);
    const ImVec2 size(bounds.GetWidth() / context.scale(), bounds.GetHeight() / context.scale());

    if (size.x != m_size.x || size.y != m_size.y) {
        m_size = size;
        context.emit(id(), "layout", {{"width", size.x}, {"height", size.y}});
    }

    paintCommands(context, bounds);

    if (m_focusable) {
        focus(context, item, bounds);
    }

    pointer(context, bounds);

    if (m_focused) {
        keys(context);
    }

    tick(context, bounds);
}

// A focusable canvas takes the keyboard on a click or when its plugin asks, and a canvas asked for anything else answers only the draw command.
Result<void> Canvas::command(RenderContext& context, std::string_view name, const json::Json& arguments) {
    if (name == "focus" && m_focusable) {
        json::ObjectReader reader(arguments, "canvas.focus");

        if (const auto finished = reader.finish(); !finished.hasValue()) {
            return finished;
        }

        m_focusWanted = true;

        return Result<void>::success();
    }

    if (name != "draw") {
        return Component::command(context, name, arguments);
    }

    const json::Json* list = &json::ObjectReader::absent();
    json::ObjectReader reader(arguments, "canvas.draw");
    reader.readArray("commands", list);

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return finished;
    }

    if (list->size() > largestList) {
        return Result<void>::failure({"ui_canvas_list_too_long", "A canvas draws at most twenty thousand commands at once", std::to_string(list->size())});
    }

    std::vector<Command> parsed;
    parsed.reserve(list->size());

    for (std::size_t index = 0; index < list->size(); ++index) {
        auto read = parse((*list)[index], index);

        if (!read.hasValue()) {
            return Result<void>::failure(read.error());
        }

        parsed.push_back(std::move(read.value()));
    }

    m_commands = std::move(parsed);

    return Result<void>::success();
}

// A command is read whole with the fields of its operation only, and the index of the first refused command names it.
Result<Canvas::Command> Canvas::parse(const json::Json& entry, std::size_t index) {
    const std::string place = "canvas.commands[" + std::to_string(index) + "]";
    Command command;
    std::string color;
    double opacity = 1.0;
    std::array<double, 4> box{0.0, 0.0, 0.0, 0.0};
    std::array<double, 2> end{0.0, 0.0};
    double radius = 0.0;
    double thickness = 1.0;
    double rotation = 0.0;
    double textSize = command.textSize;
    const json::Json* text = &json::ObjectReader::absent();
    const json::Json* source = &json::ObjectReader::absent();
    const json::Json* tile = &json::ObjectReader::absent();
    json::ObjectReader reader(entry, place);
    reader.readChoice("op", command.operation, {{"rect", Operation::Rectangle}, {"line", Operation::Line}, {"circle", Operation::Circle}, {"image", Operation::Image}, {"text", Operation::Text}, {"clip", Operation::Clip}, {"unclip", Operation::Unclip}});
    const Operation operation = command.operation;
    const bool boxed = operation == Operation::Rectangle || operation == Operation::Image || operation == Operation::Clip;
    const bool painted = operation != Operation::Image && operation != Operation::Clip && operation != Operation::Unclip;

    if (operation == Operation::Line) {
        reader.readNumber("x1", box[0], -largestCoordinate, largestCoordinate).readNumber("y1", box[1], -largestCoordinate, largestCoordinate).readNumber("x2", end[0], -largestCoordinate, largestCoordinate).readNumber("y2", end[1], -largestCoordinate, largestCoordinate);
    } else if (operation != Operation::Unclip) {
        reader.readNumber("x", box[0], -largestCoordinate, largestCoordinate).readNumber("y", box[1], -largestCoordinate, largestCoordinate);
    }

    if (boxed) {
        reader.readNumber("width", box[2], 0.0, largestCoordinate).readNumber("height", box[3], 0.0, largestCoordinate);
    }

    if (painted) {
        reader.readText("color", color);
    }

    if (operation != Operation::Clip && operation != Operation::Unclip) {
        reader.readNumber("opacity", opacity, 0.0, 1.0, json::Presence::Optional);
    }

    if (operation == Operation::Rectangle || operation == Operation::Circle) {
        reader.readNumber("radius", radius, 0.0, largestCoordinate, operation == Operation::Circle ? json::Presence::Required : json::Presence::Optional).read("filled", command.filled, json::Presence::Optional);
    }

    if (operation == Operation::Rectangle || operation == Operation::Circle || operation == Operation::Line) {
        reader.readNumber("thickness", thickness, 0.0, largestCoordinate, json::Presence::Optional);
    }

    if (operation == Operation::Image) {
        reader.readText("image", command.image).readObject("source", source, json::Presence::Optional).readObject("tile", tile, json::Presence::Optional).read("flipX", command.flipX, json::Presence::Optional).read("flipY", command.flipY, json::Presence::Optional).readNumber("rotation", rotation, -largestCoordinate, largestCoordinate, json::Presence::Optional);
    }

    if (operation == Operation::Text) {
        reader.readAny("text", text).readNumber("size", textSize, smallestText, largestText, json::Presence::Optional);
        reader.readChoice("face", command.face, {{"regular", FontFace::Regular}, {"semibold", FontFace::SemiBold}, {"monospace", FontFace::Monospace}}, json::Presence::Optional).readChoice("align", command.align, {{"start", TextAlign::Start}, {"center", TextAlign::Center}, {"end", TextAlign::End}}, json::Presence::Optional);
    }

    if (const auto finished = reader.finish(); !finished.hasValue()) {
        return Result<Command>::failure({"ui_canvas_command_invalid", "A drawing command of a canvas is invalid", finished.error().detail});
    }

    command.position = ImVec2(static_cast<float>(box[0]), static_cast<float>(box[1]));
    command.size = ImVec2(static_cast<float>(box[2]), static_cast<float>(box[3]));
    command.end = ImVec2(static_cast<float>(end[0]), static_cast<float>(end[1]));
    command.radius = static_cast<float>(radius);
    command.thickness = static_cast<float>(thickness);
    command.rotation = static_cast<float>(rotation);
    command.textSize = static_cast<float>(textSize);
    command.paint.opacity = static_cast<float>(opacity);

    if (painted) {
        auto parsed = paint(color, opacity, place + ".color");

        if (!parsed.hasValue()) {
            return Result<Command>::failure(parsed.error());
        }

        command.paint = parsed.value();
    }

    if (operation == Operation::Image && !AssetPath::safe(command.image)) {
        return Result<Command>::failure({"ui_canvas_command_invalid", "An image of a canvas is a plain path inside the assets of its plugin", place + ".image"});
    }

    if (!source->is_null()) {
        std::array<double, 4> area{0.0, 0.0, 0.0, 0.0};
        json::ObjectReader sourceReader(*source, place + ".source");
        sourceReader.readNumber("x", area[0], 0.0, largestCoordinate).readNumber("y", area[1], 0.0, largestCoordinate).readNumber("width", area[2], 0.0, largestCoordinate).readNumber("height", area[3], 0.0, largestCoordinate);

        if (const auto finished = sourceReader.finish(); !finished.hasValue()) {
            return Result<Command>::failure({"ui_canvas_command_invalid", "A drawing command of a canvas is invalid", finished.error().detail});
        }

        command.source = ImRect(static_cast<float>(area[0]), static_cast<float>(area[1]), static_cast<float>(area[0] + area[2]), static_cast<float>(area[1] + area[3]));
    }

    // A tiled picture repeats at the size of its tile across its rectangle, never turns and stops at a bound of tiles, so one command never floods the renderer.
    if (!tile->is_null()) {
        std::array<double, 2> step{0.0, 0.0};
        json::ObjectReader tileReader(*tile, place + ".tile");
        tileReader.readNumber("width", step[0], smallestTile, largestCoordinate).readNumber("height", step[1], smallestTile, largestCoordinate);

        if (const auto finished = tileReader.finish(); !finished.hasValue()) {
            return Result<Command>::failure({"ui_canvas_command_invalid", "A drawing command of a canvas is invalid", finished.error().detail});
        }

        if (rotation != 0.0 || std::ceil(box[2] / step[0]) * std::ceil(box[3] / step[1]) > mostTiles) {
            return Result<Command>::failure({"ui_canvas_command_invalid", "A tiled picture of a canvas never turns and repeats at most 4096 times", place + ".tile"});
        }

        command.tile = ImVec2(static_cast<float>(step[0]), static_cast<float>(step[1]));
    }

    if (operation == Operation::Text) {
        auto parsed = TextValue::parse(*text, place + ".text");

        if (!parsed.hasValue()) {
            return Result<Command>::failure({"ui_canvas_command_invalid", "A text of a canvas is a literal or a translation reference", parsed.error().detail});
        }

        command.text = std::move(parsed.value());
    }

    return Result<Command>::success(std::move(command));
}

// A color is a role of the theme, drawn in the theme the reader chose, or a literal color written with a number sign and six hexadecimal digits.
Result<Canvas::Paint> Canvas::paint(const std::string& color, double opacity, const std::string& place) {
    Paint result;
    result.opacity = static_cast<float>(opacity);

    if (color.starts_with("#")) {
        const auto literal = Color::parse(color);

        if (!literal.has_value()) {
            return Result<Paint>::failure({"ui_canvas_command_invalid", "A color of a canvas is a role of the theme or a number sign and six hexadecimal digits", place});
        }

        result.literal = *literal;

        return Result<Paint>::success(result);
    }

    const auto role = ThemeColorNames::parse(color);

    if (!role.has_value()) {
        return Result<Paint>::failure({"ui_canvas_command_invalid", "A color of a canvas is a role of the theme or a number sign and six hexadecimal digits", place});
    }

    result.role = role;

    return Result<Paint>::success(result);
}

Color Canvas::colorOf(const RenderContext& context, const Paint& paint) {
    const Color color = paint.role.has_value() ? context.color(*paint.role) : paint.literal;
    return color.withAlpha(paint.opacity);
}

// The canvas ticks at its frame rate while it is drawn, telling its plugin the seconds since the last tick, bounded after a pause, and asks the loop for its next tick instead of keeping it awake.
void Canvas::tick(RenderContext& context, const ImRect&) {
    if (m_frameRate <= 0) {
        m_lastTick = -1.0;
        return;
    }

    const double now = context.time();
    const double period = 1.0 / static_cast<double>(m_frameRate);

    if (m_lastTick >= 0.0 && now < m_nextTick) {
        context.requestFrameAt(m_nextTick);
        return;
    }

    const double delta = m_lastTick < 0.0 ? 0.0 : std::min(now - m_lastTick, longestTick);
    context.report(id(), "frame", {{"delta", delta}, {"time", now}, {"width", m_size.x}, {"height", m_size.y}});
    m_lastTick = now;
    m_nextTick = m_nextTick + period > now ? m_nextTick + period : now + period;
    context.requestFrameAt(m_nextTick);
}

// A click on the canvas gives it the keyboard, and it keeps every key while it has it except the combinations of the product.
void Canvas::focus(RenderContext& context, ImGuiID item, const ImRect& bounds) {
    const bool hovered = ImGui::IsMouseHoveringRect(bounds.Min, bounds.Max) && ImGui::IsWindowHovered();
    const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);

    if (m_focusWanted || (hovered && clicked)) {
        ImGui::SetActiveID(item, ImGui::GetCurrentWindow());
        ImGui::SetFocusID(item, ImGui::GetCurrentWindow());
        m_focusWanted = false;
    }

    if (!hovered && clicked && ImGui::GetActiveID() == item) {
        ImGui::ClearActiveID();
    }

    const bool focused = ImGui::GetActiveID() == item;

    if (focused && ImGui::GetIO().KeyCtrl) {
        GImGui->ActiveIdUsingAllKeyboardKeys = false;
    } else if (focused) {
        ImGui::SetActiveIdUsingAllKeyboardKeys();
    }

    if (focused) {
        GImGui->ActiveIdAllowOverlap = true;
    }

    if (focused == m_focused) {
        return;
    }

    // A canvas that loses the keyboard lets go of every key it told as held, since their release reaches another control.
    if (!focused) {
        for (const std::string& name : std::exchange(m_held, {})) {
            context.emit(id(), "key-up", {{"key", name}});
        }
    }

    m_focused = focused;
    context.emit(id(), "focus", {{"focused", focused}});
}

// The pointer is told in points from the top left corner of the canvas: its presses and releases, how far the wheel scrolls in points toward the right and the bottom, and its moves while the canvas tracks it.
void Canvas::pointer(RenderContext& context, const ImRect& bounds) {
    const ImGuiIO& io = ImGui::GetIO();

    if (!ImGui::IsMouseHoveringRect(bounds.Min, bounds.Max) || !ImGui::IsWindowHovered()) {
        m_pointer = ImVec2(-1.0F, -1.0F);
        return;
    }

    const ImVec2 local((io.MousePos.x - bounds.Min.x) / context.scale(), (io.MousePos.y - bounds.Min.y) / context.scale());
    const std::array<std::pair<ImGuiMouseButton, const char*>, 3> buttons{{{ImGuiMouseButton_Left, "left"}, {ImGuiMouseButton_Right, "right"}, {ImGuiMouseButton_Middle, "middle"}}};

    for (const auto& [button, name] : buttons) {
        if (ImGui::IsMouseClicked(button)) {
            context.emit(id(), "pointer-down", {{"x", local.x}, {"y", local.y}, {"button", name}});
        }

        if (ImGui::IsMouseReleased(button)) {
            context.emit(id(), "pointer-up", {{"x", local.x}, {"y", local.y}, {"button", name}});
        }
    }

    const ImVec2 moved = WheelScale::distance();

    if (moved.x != 0.0F || moved.y != 0.0F) {
        context.emit(id(), "wheel", {{"x", local.x}, {"y", local.y}, {"deltaX", -moved.x / context.scale()}, {"deltaY", -moved.y / context.scale()}});
    }

    if (m_tracking && (local.x != m_pointer.x || local.y != m_pointer.y)) {
        context.emit(id(), "pointer-move", {{"x", local.x}, {"y", local.y}});
    }

    m_pointer = local;
}

// A focused canvas reports every key the shortcut grammar names, pressed and released, by that name.
void Canvas::keys(RenderContext& context) {
    for (const auto& [name, key] : KeyChords::named()) {
        if (ImGui::IsKeyPressed(key, false)) {
            m_held.push_back(name);
            context.emit(id(), "key-down", {{"key", name}});
        }

        if (ImGui::IsKeyReleased(key)) {
            std::erase(m_held, name);
            context.emit(id(), "key-up", {{"key", name}});
        }
    }
}

// Commands draw in order inside the canvas, clips nest until they are undone or the list ends, and a pixelated canvas samples its pictures by their nearest pixel and gives the renderer back its linear sampling.
void Canvas::paintCommands(RenderContext& context, const ImRect& bounds) {
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
    const bool nearest = m_pixelated && platform.DrawCallback_SetSamplerNearest != nullptr && platform.DrawCallback_SetSamplerLinear != nullptr;
    const float scale = context.scale();
    int clips = 0;
    list.PushClipRect(bounds.Min, bounds.Max, true);

    if (m_background.has_value()) {
        list.AddRectFilled(bounds.Min, bounds.Max, Widgets::ink(colorOf(context, *m_background)));
    }

    if (nearest) {
        list.AddCallback(platform.DrawCallback_SetSamplerNearest, nullptr);
    }

    for (const Command& command : m_commands) {
        const ImVec2 at(bounds.Min.x + command.position.x * scale, bounds.Min.y + command.position.y * scale);
        const ImVec2 far(at.x + command.size.x * scale, at.y + command.size.y * scale);

        switch (command.operation) {
        case Operation::Rectangle:
            if (command.filled) {
                list.AddRectFilled(at, far, Widgets::ink(colorOf(context, command.paint)), command.radius * scale);
            } else {
                list.AddRect(at, far, Widgets::ink(colorOf(context, command.paint)), command.radius * scale, command.thickness * scale);
            }

            break;
        case Operation::Line:
            list.AddLine(at, ImVec2(bounds.Min.x + command.end.x * scale, bounds.Min.y + command.end.y * scale), Widgets::ink(colorOf(context, command.paint)), command.thickness * scale);
            break;
        case Operation::Circle:
            if (command.filled) {
                list.AddCircleFilled(at, command.radius * scale, Widgets::ink(colorOf(context, command.paint)));
            } else {
                list.AddCircle(at, command.radius * scale, Widgets::ink(colorOf(context, command.paint)), 0, command.thickness * scale);
            }

            break;
        case Operation::Image:
            drawImage(context, list, bounds.Min, command, nearest);
            break;
        case Operation::Text:
            drawText(context, list, at, command);
            break;
        case Operation::Clip:
            list.PushClipRect(at, far, true);
            ++clips;
            break;
        case Operation::Unclip:
            if (clips > 0) {
                list.PopClipRect();
                --clips;
            }

            break;
        }
    }

    for (; clips > 0; --clips) {
        list.PopClipRect();
    }

    if (nearest) {
        list.AddCallback(platform.DrawCallback_SetSamplerLinear, nullptr);
    }

    list.PopClipRect();
}

// A text starts, centers or ends at its point, in the face and the size its command names.
void Canvas::drawText(RenderContext& context, ImDrawList& list, const ImVec2& at, const Command& command) {
    const FontRole role{command.face, command.textSize};
    const std::string& written = context.text(command.text);
    const float width = Widgets::textSize(context, role, written).x;
    const float shift = command.align == TextAlign::Center ? width / 2.0F : command.align == TextAlign::End ? width : 0.0F;
    Widgets::text(context, list, role, ImVec2(at.x - shift, at.y), colorOf(context, command.paint), written);
}

// A picture takes its source rectangle in pixels, flips on either axis and turns around its center, and one that cannot be read is told to the plugin once.
void Canvas::drawImage(RenderContext& context, ImDrawList& list, const ImVec2& origin, const Command& command, bool nearest) {
    const Texture& texture = context.textures().request(context.assets() / command.image);

    if (texture.state == TextureState::Failed && std::ranges::find(m_failedImages, command.image) == m_failedImages.end()) {
        m_failedImages.push_back(command.image);
        context.emit(id(), "image-error", {{"image", command.image}, {"message", texture.failure}});
    }

    if (texture.state != TextureState::Ready || texture.width <= 0 || texture.height <= 0) {
        return;
    }

    const float scale = context.scale();
    const auto width = static_cast<float>(texture.width);
    const auto height = static_cast<float>(texture.height);
    const ImRect source = command.source.value_or(ImRect(0.0F, 0.0F, width, height));
    // A frame of a sheet is sampled a sliver of a pixel inside its edges, or half a pixel inside when sampled linearly, so an edge that lands on the center of a pixel never shows the frame beside it.
    const float inset = !command.source.has_value() ? 0.0F : nearest ? nearestInset : linearInset;
    const ImVec2 edge(std::min(inset, source.GetWidth() / 2.0F), std::min(inset, source.GetHeight() / 2.0F));
    ImVec2 uvMin((source.Min.x + edge.x) / width, (source.Min.y + edge.y) / height);
    ImVec2 uvMax((source.Max.x - edge.x) / width, (source.Max.y - edge.y) / height);

    if (command.flipX) {
        std::swap(uvMin.x, uvMax.x);
    }

    if (command.flipY) {
        std::swap(uvMin.y, uvMax.y);
    }

    const ImVec2 at(origin.x + command.position.x * scale, origin.y + command.position.y * scale);
    const ImVec2 size(command.size.x * scale, command.size.y * scale);
    const ImU32 tint = Widgets::ink(Color::rgb(255, 255, 255).withAlpha(command.paint.opacity));

    // A tiled picture repeats from the corner of its rectangle and is cut at its far edges.
    if (command.tile.has_value()) {
        const ImVec2 step(command.tile->x * scale, command.tile->y * scale);
        const auto columns = static_cast<int>(std::ceil(size.x / step.x));
        const auto rows = static_cast<int>(std::ceil(size.y / step.y));
        list.PushClipRect(at, ImVec2(at.x + size.x, at.y + size.y), true);

        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                const ImVec2 corner(at.x + static_cast<float>(column) * step.x, at.y + static_cast<float>(row) * step.y);
                list.AddImage(texture.reference(), corner, ImVec2(corner.x + step.x, corner.y + step.y), uvMin, uvMax, tint);
            }
        }

        list.PopClipRect();
        return;
    }

    if (command.rotation == 0.0F) {
        list.AddImage(texture.reference(), at, ImVec2(at.x + size.x, at.y + size.y), uvMin, uvMax, tint);
        return;
    }

    // A turned picture is a quad whose corners turn around the center of its rectangle.
    const ImVec2 center(at.x + size.x / 2.0F, at.y + size.y / 2.0F);
    const float cosine = std::cos(command.rotation);
    const float sine = std::sin(command.rotation);
    std::array<ImVec2, 4> corners{ImVec2(-size.x / 2.0F, -size.y / 2.0F), ImVec2(size.x / 2.0F, -size.y / 2.0F), ImVec2(size.x / 2.0F, size.y / 2.0F), ImVec2(-size.x / 2.0F, size.y / 2.0F)};

    for (ImVec2& corner : corners) {
        corner = ImVec2(center.x + corner.x * cosine - corner.y * sine, center.y + corner.x * sine + corner.y * cosine);
    }

    list.AddImageQuad(texture.reference(), corners[0], corners[1], corners[2], corners[3], uvMin, ImVec2(uvMax.x, uvMin.y), uvMax, ImVec2(uvMin.x, uvMax.y), tint);
}

} // namespace workpane::ui
