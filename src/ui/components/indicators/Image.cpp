#include "ui/components/indicators/Image.h"

#include "ui/AssetPath.h"
#include "ui/Texture.h"
#include "ui/TextureCache.h"

#include <algorithm>
#include <utility>

namespace workpane::ui {

Image::Image(NodeId id) : Component(id) {}

std::string_view Image::kind() const {
    return "image";
}

Alignment Image::defaultColumnAlignment() const {
    return Alignment::Center;
}

void Image::readProperties(json::ObjectReader& reader) {
    if (reader.contains("source")) {
        std::string source;
        reader.readText("source", source);

        if (!AssetPath::safe(source)) {
            fail({"ui_image_source_invalid", "An image source must be a plain path inside the assets of its plugin", "image.source"});
            return;
        }

        m_reported = m_reported && source == m_source;
        m_source = std::move(source);
    }

    reader.readChoice("shape", m_shape, {{"rectangle", ImageShape::Rectangle}, {"circle", ImageShape::Circle}}, json::Presence::Optional);
}

Component::Restore Image::keep() {
    return kept(m_reported, m_source, m_shape);
}

Result<void> Image::validate() const {
    if (m_source.empty()) {
        return Result<void>::failure({"ui_image_source_missing", "An image names the picture it shows", "image.source"});
    }

    return Result<void>::success();
}

// A picture is as many points as it has pixels, like every other length the layout counts.
ImVec2 Image::measureContent(RenderContext& context, float) {
    const Texture& texture = context.textures().request(context.assets() / m_source);

    if (texture.state != TextureState::Ready) {
        return {0.0F, 0.0F};
    }

    return {static_cast<float>(texture.width) * context.scale(), static_cast<float>(texture.height) * context.scale()};
}

// A circle covers its square with the middle of the picture, and a rectangle shows the whole picture at its own proportion.
void Image::render(RenderContext& context, const ImRect& bounds) {
    const Texture& texture = context.textures().request(context.assets() / m_source);

    if (texture.state == TextureState::Failed && !m_reported) {
        m_reported = true;
        context.emit(id(), "error", {{"code", "ui_image_unreadable"}, {"source", m_source}, {"message", texture.failure}});
    }

    if (texture.state != TextureState::Ready || texture.width <= 0 || texture.height <= 0) {
        return;
    }

    ImDrawList& list = *ImGui::GetWindowDrawList();
    const float aspect = static_cast<float>(texture.width) / static_cast<float>(texture.height);

    if (m_shape == ImageShape::Circle) {
        const float diameter = std::min(bounds.GetWidth(), bounds.GetHeight());
        const float radius = diameter / 2.0F;
        const ImVec2 center = bounds.GetCenter();
        const ImVec2 uvMin = aspect > 1.0F ? ImVec2((1.0F - 1.0F / aspect) / 2.0F, 0.0F) : ImVec2(0.0F, (1.0F - aspect) / 2.0F);
        const ImVec2 uvMax(1.0F - uvMin.x, 1.0F - uvMin.y);
        list.AddImageRounded(texture.reference(), ImVec2(center.x - radius, center.y - radius), ImVec2(center.x + radius, center.y + radius), uvMin, uvMax, IM_COL32_WHITE, radius);
        return;
    }

    const float width = std::min(bounds.GetWidth(), bounds.GetHeight() * aspect);
    const float height = width / aspect;
    const ImVec2 origin(bounds.GetCenter().x - width / 2.0F, bounds.GetCenter().y - height / 2.0F);
    list.AddImage(texture.reference(), origin, ImVec2(origin.x + width, origin.y + height));
}

} // namespace workpane::ui
