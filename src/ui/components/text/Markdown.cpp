#include "ui/components/text/Markdown.h"

#include "json/ObjectReader.h"
#include "platform/PathTextHelper.h"
#include "text/Utf8Helper.h"
#include "ui/ButtonState.h"
#include "ui/Color.h"
#include "ui/NativeViewHost.h"
#include "ui/Painter.h"
#include "ui/Texture.h"
#include "ui/TextureCache.h"
#include "ui/WidgetHelper.h"
#include "ui/model/ContentFontSize.h"
#include "ui/model/EventSink.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <string_view>

namespace workpane::ui {

Markdown::Markdown(NodeId id) : Component(id) {}

std::string_view Markdown::kind() const {
    return "markdown";
}

void Markdown::readProperties(json::ObjectReader& reader) {
    readText(reader, "text", m_text);
    readColor(reader, "color", m_color);
    reader.read("breaks", m_breaks, json::Presence::Optional);
    reader.read("pathLinks", m_pathLinks, json::Presence::Optional);
    reader.read("basePath", m_imageRoot, json::Presence::Optional);

    if (m_imageRoot.find('\0') != std::string::npos || (!m_imageRoot.empty() && !std::filesystem::path(std::u8string(m_imageRoot.begin(), m_imageRoot.end())).is_absolute())) {
        fail({"ui_markdown_base_path_invalid", "The base path of Markdown images must be absolute", "markdown.basePath"});
    }

    if (reader.contains("fontSize")) {
        double size = 0.0;
        reader.readNumber("fontSize", size, ContentFontSize::minimum, ContentFontSize::maximum);
        m_fontSize = static_cast<float>(size);
    }

    m_layoutValid = false;
}

Component::Restore Markdown::keep() {
    return kept(m_text, m_color, m_breaks, m_fontSize, m_layoutValid, m_pathLinks, m_imageRoot);
}

Result<void> Markdown::command(RenderContext& context, std::string_view name, const json::Json& arguments) {
    if (name != "load-image") {
        return Component::command(context, name, arguments);
    }

    json::ObjectReader reader(arguments, "markdown.load-image");
    std::string source;
    ImageResource image;
    reader.readText("source", source);
    reader.readInteger("request", image.request, 1, std::numeric_limits<std::int64_t>::max());
    reader.readText("file", image.file, json::Presence::Optional);
    reader.readText("data", image.data, json::Presence::Optional);
    reader.read("failed", image.failed, json::Presence::Optional);
    const auto read = reader.finish();

    if (!read.hasValue()) {
        return read;
    }

    const int alternatives = static_cast<int>(!image.file.empty()) + static_cast<int>(!image.data.empty()) + static_cast<int>(image.failed);

    if (alternatives != 1 || image.file.find('\0') != std::string::npos || (!image.file.empty() && !std::filesystem::path(std::u8string(image.file.begin(), image.file.end())).is_absolute()) || (!image.data.empty() && (!image.data.starts_with("data:image/") || image.data.size() > maximumImageBytes))) {
        return Result<void>::failure({"ui_markdown_image_invalid", "An image reply carries one absolute file, a bounded image data address or a failure", "markdown.load-image"});
    }

    const auto found = m_images.find(source);

    if (found == m_images.end() || found->second.request != image.request) {
        return Result<void>::success();
    }

    image.requested = true;
    found->second = std::move(image);
    m_layoutValid = false;
    context.requestFrame();

    return Result<void>::success();
}

ImVec2 Markdown::measureContent(RenderContext& context, float availableWidth) {
    return layout(context, availableWidth).size;
}

void Markdown::render(RenderContext& context, const ImRect& bounds) {
    const markdown::Layout& laid = layout(context, bounds.GetWidth());
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const float scale = context.scale();
    select(context, bounds, laid);

    for (const auto& shape : laid.shapes) {
        const ImVec2 minimum = bounds.Min + shape.rect.Min;
        const ImVec2 maximum = bounds.Min + ImVec2(shape.reachesEdge ? bounds.GetWidth() : shape.rect.Max.x, shape.rect.Max.y);

        switch (shape.kind) {
        case markdown::ShapeKind::Surface:
            list.AddRectFilled(minimum, maximum, WidgetHelper::ink(context.color(ThemeColor::Terminal)), context.metric(ThemeMetric::ControlRadius));
            break;
        case markdown::ShapeKind::Bar:
            list.AddRectFilled(minimum, maximum, WidgetHelper::ink(context.color(ThemeColor::BorderStrong)));
            break;
        case markdown::ShapeKind::Rule:
            Painter::horizontalDivider(list, minimum, maximum.x - minimum.x, context.metric(ThemeMetric::LineWidth), context.color(ThemeColor::Border));
            break;
        case markdown::ShapeKind::Clip:
            break;
        }
    }

    // Links are registered as items before anything is painted, so every run of a link wrapped over lines shows the hover together.
    int hovered = -1;

    for (std::size_t index = 0; index < laid.runs.size(); ++index) {
        const markdown::Run& run = laid.runs[index];

        if (run.link < 0) {
            continue;
        }

        const ImVec2 position = bounds.Min + run.offset;
        ImRect target(position, position + ImVec2(run.width, run.height));

        if (run.clip >= 0) {
            const auto& surface = laid.shapes[static_cast<std::size_t>(run.clip)];
            target.ClipWithFull(ImRect(bounds.Min + surface.rect.Min, bounds.Min + ImVec2(surface.reachesEdge ? bounds.GetWidth() : surface.rect.Max.x, surface.rect.Max.y)));
        }

        if (target.GetWidth() < 1.0F || target.GetHeight() < 1.0F) {
            continue;
        }

        const ButtonState state = WidgetHelper::interact(ImGui::GetID(static_cast<int>(index)), target);

        if (state.hovered) {
            hovered = run.link;
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }

        if (state.pressed && m_anchor == m_caret) {
            context.emit(id(), "link", {{"url", laid.links[static_cast<std::size_t>(run.link)]}});
        }
    }

    if (m_selecting && m_anchor != m_caret) {
        ImGui::SetActiveID(ImGui::GetID("##selection"), ImGui::GetCurrentWindow());
    }

    for (const auto& run : laid.runs) {
        const ImVec2 position = bounds.Min + run.offset;
        const float glyphs = WidgetHelper::fontSize(context, run.font);
        const float baseline = position.y + (run.height - glyphs) / 2.0F;
        const bool linkHovered = run.link >= 0 && run.link == hovered;
        const Color color = run.color.has_value() ? context.color(*run.color) : ink(context, run.ink, linkHovered);

        if (run.clip >= 0) {
            const auto& surface = laid.shapes[static_cast<std::size_t>(run.clip)];
            list.PushClipRect(bounds.Min + surface.rect.Min, bounds.Min + ImVec2(surface.reachesEdge ? bounds.GetWidth() : surface.rect.Max.x, surface.rect.Max.y), true);
        }

        if (run.code) {
            const float padding = codeSpanPadding * scale;
            list.AddRectFilled(ImVec2(position.x - padding, baseline - scale), ImVec2(position.x + run.width + padding, baseline + glyphs + 2.0F * scale), WidgetHelper::ink(context.color(ThemeColor::Terminal)), codeSpanRadius * scale);
        }

        const std::size_t first = std::max(run.textOffset, std::min(m_anchor, m_caret));
        const std::size_t last = std::min(run.textOffset + run.text.size(), std::max(m_anchor, m_caret));

        if (!run.image.empty()) {
            const Texture* texture = imageTexture(context, run.image);

            if (texture != nullptr && texture->state == TextureState::Ready) {
                list.AddImage(texture->reference(), position, position + ImVec2(run.width, run.height));

                if (first < last) {
                    list.AddRect(position, position + ImVec2(run.width, run.height), WidgetHelper::ink(context.color(ThemeColor::Selection)), 0.0F, context.metric(ThemeMetric::LineWidth));
                }
            }

            if (run.clip >= 0) {
                list.PopClipRect();
            }

            continue;
        }

        if (first < last) {
            const float left = WidgetHelper::textSize(context, run.font, std::string_view(run.text).substr(0, first - run.textOffset)).x;
            const float right = WidgetHelper::textSize(context, run.font, std::string_view(run.text).substr(0, last - run.textOffset)).x;
            list.AddRectFilled(position + ImVec2(left, 0.0F), position + ImVec2(right, run.height), WidgetHelper::ink(context.color(ThemeColor::Selection)));
        }

        WidgetHelper::text(context, list, run.font, ImVec2(position.x, baseline), color, run.text);

        if (run.strike) {
            list.AddLine(ImVec2(position.x, baseline + glyphs / 2.0F), ImVec2(position.x + run.width, baseline + glyphs / 2.0F), WidgetHelper::ink(color), context.metric(ThemeMetric::LineWidth));
        }

        if (linkHovered) {
            const float underline = std::round(baseline + glyphs + scale);
            list.AddLine(ImVec2(position.x, underline), ImVec2(position.x + run.width, underline), WidgetHelper::ink(color), std::max(1.0F, std::round(scale)));
        }

        if (run.clip >= 0) {
            list.PopClipRect();
        }
    }
}

std::size_t Markdown::textPosition(RenderContext& context, const markdown::Layout& laid, ImVec2 point) const {
    const markdown::Run* nearest = nullptr;
    float distance = FLT_MAX;

    for (const auto& run : laid.runs) {
        const float vertical = point.y < run.offset.y ? run.offset.y - point.y : std::max(0.0F, point.y - run.offset.y - run.height);
        const float horizontal = point.x < run.offset.x ? run.offset.x - point.x : std::max(0.0F, point.x - run.offset.x - run.width);
        const float candidate = vertical * vertical + horizontal * horizontal;

        if (candidate < distance) {
            nearest = &run;
            distance = candidate;
        }
    }

    if (nearest == nullptr) {
        return 0;
    }

    if (!nearest->image.empty()) {
        return nearest->textOffset + (point.x > nearest->offset.x + nearest->width / 2.0F ? nearest->text.size() : 0U);
    }

    std::size_t offset = 0;
    float left = 0.0F;

    while (offset < nearest->text.size()) {
        const std::size_t next = std::min(nearest->text.size(), offset + std::max(std::size_t{1}, text::Utf8Helper::sequenceLength(static_cast<unsigned char>(nearest->text[offset]))));
        const float right = WidgetHelper::textSize(context, nearest->font, std::string_view(nearest->text).substr(0, next)).x;

        if (point.x - nearest->offset.x < (left + right) / 2.0F) {
            break;
        }

        offset = next;
        left = right;
    }

    return nearest->textOffset + offset;
}

void Markdown::select(RenderContext& context, const ImRect& bounds, const markdown::Layout& laid) {
    const ImGuiIO& io = ImGui::GetIO();
    const ImGuiID selection = ImGui::GetID("##selection");
    const bool hovered = !WidgetHelper::disabled() && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(bounds.Min, bounds.Max);

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_selecting = hovered;

        if (hovered) {
            context.nativeViews().focusProduct();
            ImGui::SetActiveID(selection, ImGui::GetCurrentWindow());
            ImGui::FocusWindow(ImGui::GetCurrentWindow());
            GImGui->ActiveIdAllowOverlap = true;
            m_caret = textPosition(context, laid, io.MousePos - bounds.Min);
            m_anchor = io.KeyShift ? m_anchor : m_caret;
        } else if (GImGui->ActiveId == selection) {
            ImGui::ClearActiveID();
        }
    }

    const bool focused = !WidgetHelper::disabled() && GImGui->ActiveId == selection;

    if (focused) {
        ImGui::KeepAliveID(selection);
    }

    if (m_selecting && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_caret = textPosition(context, laid, io.MousePos - bounds.Min);
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_selecting = false;
    }

    if (focused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
        m_anchor = 0;
        m_caret = laid.text.size();
    }

    m_anchor = std::min(m_anchor, laid.text.size());
    m_caret = std::min(m_caret, laid.text.size());

    if (focused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && m_anchor != m_caret) {
        const std::size_t first = std::min(m_anchor, m_caret);
        const std::size_t count = std::max(m_anchor, m_caret) - first;
        ImGui::SetClipboardText(laid.text.substr(first, count).c_str());
    }
}

bool Markdown::filePath(std::string_view text) {
    if (text.empty() || text.find_first_of("\n\t") != std::string_view::npos) {
        return false;
    }

    if (text.starts_with("/") || text.starts_with("./") || text.starts_with("../") || text.starts_with("~/") || text.starts_with("file://") || (text.size() > 2 && text[1] == ':' && (text[2] == '/' || text[2] == '\\'))) {
        return true;
    }

    if (text.find(' ') != std::string_view::npos) {
        return false;
    }

    if (text.find('/') != std::string_view::npos || text.find('\\') != std::string_view::npos) {
        return true;
    }

    text = text.substr(0, text.find(':'));
    const auto dot = text.rfind('.');
    const std::string_view extension = dot == std::string_view::npos ? "" : text.substr(dot + 1);
    static constexpr std::array extensions{"c", "cpp", "cc", "h", "hpp", "js", "jsx", "ts", "tsx", "vue", "svelte", "lua", "py", "rs", "go", "json", "md", "txt", "yaml", "yml", "toml", "xml", "html", "css", "scss", "sh", "sql", "lock", "cmake"};

    return std::ranges::find(extensions, extension) != extensions.end();
}

void Markdown::linkPaths(std::vector<markdown::Span>& spans) {
    for (auto& span : spans) {
        if (span.code && span.link.empty() && filePath(span.text)) {
            span.link = span.text;
        }
    }
}

// A layout that already fits a narrower width than it was built for is kept, because breaking the same words again places them where they are.
const markdown::Layout& Markdown::layout(RenderContext& context, float limit) {
    const std::string& source = context.text(m_text);

    if (source != m_source || m_breaks != m_parsedBreaks || m_pathLinks != m_parsedPathLinks || m_imageRoot != m_parsedImageRoot) {
        if (m_imageRoot != m_parsedImageRoot) {
            m_images.clear();
            m_imageSizes.clear();
        }

        m_source = source;
        m_parsedBreaks = m_breaks;
        m_parsedPathLinks = m_pathLinks;
        m_parsedImageRoot = m_imageRoot;
        m_blocks = markdown::Parser::parse(m_source, m_breaks);
        std::set<std::string, std::less<>> sources;

        for (const auto& block : m_blocks) {
            gatherImages(block.spans, sources);

            for (const auto& row : block.rows) {
                for (const auto& cell : row) {
                    gatherImages(cell.spans, sources);
                }
            }
        }

        // clang-format off
        std::erase_if(m_images, [&sources](const auto& entry) { return !sources.contains(entry.first); });
        // clang-format on

        for (const auto& image : sources) {
            m_images.try_emplace(image);
        }

        if (m_pathLinks) {
            for (auto& block : m_blocks) {
                linkPaths(block.spans);

                for (auto& row : block.rows) {
                    for (auto& cell : row) {
                        linkPaths(cell.spans);
                    }
                }
            }
        }

        m_layoutValid = false;
    }

    prepareImages(context);

    const bool reusable = m_layoutValid && m_layoutScale == context.scale() && m_layout.size.x <= limit && limit <= m_layout.limit;

    if (!reusable) {
        FontRole body = context.font(ThemeFont::Interface);

        if (m_fontSize.has_value()) {
            body.size = *m_fontSize;
        }

        m_layout = markdown::LayoutBuilder(context, body, limit, m_imageSizes).build(m_blocks);
        m_layoutScale = context.scale();
        m_layoutValid = true;
    }

    return m_layout;
}

void Markdown::gatherImages(const std::vector<markdown::Span>& spans, std::set<std::string, std::less<>>& sources) {
    for (const auto& span : spans) {
        if (!span.image.empty()) {
            sources.insert(span.image);
        }
    }
}

void Markdown::prepareImages(RenderContext& context) {
    markdown::LayoutBuilder::ImageSizes sizes;

    for (auto& [source, resource] : m_images) {
        if (!resource.requested) {
            resource.requested = true;
            resource.request = ++m_nextImageRequest;
            context.emit(id(), "image-request", {{"source", source}, {"request", resource.request}, {"basePath", m_imageRoot.empty() ? platform::PathTextHelper::generic(context.assets()) : m_imageRoot}});
        }

        const Texture* texture = imageTexture(context, source);

        if (texture != nullptr && texture->state == TextureState::Ready) {
            resource.size = ImVec2(static_cast<float>(texture->width), static_cast<float>(texture->height));
        }

        if (texture != nullptr && texture->state != TextureState::Failed && resource.size.x > 0.0F && resource.size.y > 0.0F) {
            sizes[source] = resource.size * context.scale();
        }
    }

    bool changed = sizes.size() != m_imageSizes.size();

    for (const auto& [source, size] : sizes) {
        const auto found = m_imageSizes.find(source);
        changed = changed || found == m_imageSizes.end() || found->second.x != size.x || found->second.y != size.y;
    }

    if (changed) {
        m_imageSizes = std::move(sizes);
        m_layoutValid = false;
    }
}

const Texture* Markdown::imageTexture(RenderContext& context, const std::string& source) const {
    const auto found = m_images.find(source);

    if (found == m_images.end() || found->second.failed) {
        return nullptr;
    }

    if (!found->second.data.empty()) {
        return &context.textures().requestPictureData(found->second.data);
    }

    if (!found->second.file.empty()) {
        return &context.textures().request(std::filesystem::path(std::u8string(found->second.file.begin(), found->second.file.end())));
    }

    return nullptr;
}

Color Markdown::ink(RenderContext& context, markdown::Ink role, bool hovered) const {
    if (WidgetHelper::disabled()) {
        return context.color(ThemeColor::TextMuted);
    }

    switch (role) {
    case markdown::Ink::Muted:
        return context.color(ThemeColor::TextMuted);
    case markdown::Ink::Link:
        return context.color(hovered ? ThemeColor::AccentText : ThemeColor::Accent);
    case markdown::Ink::Text:
        break;
    }

    return context.color(m_color.value_or(ThemeColor::Text));
}

} // namespace workpane::ui
