#include "ui/components/containers/LinearContainer.h"

#include "ui/Color.h"
#include "ui/Painter.h"
#include "ui/WidgetHelper.h"

#include <algorithm>
#include <numeric>
#include <string>

namespace workpane::ui {

LinearContainer::LinearContainer(NodeId id, Axis axis) : Component(id), m_axis(axis) {}

std::size_t LinearContainer::childLimit() const {
    return unlimitedChildren;
}

Alignment LinearContainer::defaultRowAlignment() const {
    return Alignment::Stretch;
}

void LinearContainer::readProperties(json::ObjectReader& reader) {
    readInsets(reader, "padding", m_padding);
    readSpacing(reader, "spacing", m_spacing);
    reader.readChoice("justify", m_justify, {{"start", Justify::Start}, {"center", Justify::Center}, {"end", Justify::End}, {"space-between", Justify::SpaceBetween}}, json::Presence::Optional);
    readColor(reader, "background", m_background);

    if (!reader.contains("borders")) {
        return;
    }

    std::vector<std::string> sides;
    reader.read("borders", sides);
    Borders borders;

    for (const auto& side : sides) {
        if (side == "top") {
            borders.top = true;
        } else if (side == "right") {
            borders.right = true;
        } else if (side == "bottom") {
            borders.bottom = true;
        } else if (side == "left") {
            borders.left = true;
        } else {
            fail({"ui_border_unknown", "A border names a side a rectangle does not have", std::string(kind()) + ".borders=" + side});
            return;
        }
    }

    m_borders = borders;
}

Component::Restore LinearContainer::keep() {
    return kept(m_padding, m_spacing, m_justify, m_background, m_borders);
}

ImVec2 LinearContainer::measureContent(RenderContext& context, float availableWidth) {
    return m_axis == Axis::Vertical ? measureVertical(context, availableWidth) : measureHorizontal(context, availableWidth);
}

void LinearContainer::render(RenderContext& context, const ImRect& bounds) {
    paintSurface(context, bounds);
    const ImRect content = contentInsets(context).shrink(bounds);

    if (m_axis == Axis::Vertical) {
        renderVertical(context, content);
        return;
    }

    renderHorizontal(context, content);
}

// A surface painted behind the children, which a card draws with its own shape.
void LinearContainer::paintSurface(RenderContext& context, const ImRect& bounds) {
    ImDrawList& list = *ImGui::GetWindowDrawList();

    if (m_background.has_value()) {
        list.AddRectFilled(bounds.Min, bounds.Max, WidgetHelper::ink(context.color(*m_background)));
    }

    paintBorders(context, bounds);
}

void LinearContainer::paintBorders(RenderContext& context, const ImRect& bounds) const {
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const Color border = context.color(ThemeColor::Border);
    const float line = context.metric(ThemeMetric::LineWidth);

    if (m_borders.top) {
        Painter::horizontalDivider(list, bounds.Min, bounds.GetWidth(), line, border);
    }

    if (m_borders.bottom) {
        Painter::horizontalDivider(list, ImVec2(bounds.Min.x, bounds.Max.y - line), bounds.GetWidth(), line, border);
    }

    if (m_borders.left) {
        Painter::verticalDivider(list, bounds.Min, bounds.GetHeight(), line, border);
    }

    if (m_borders.right) {
        Painter::verticalDivider(list, ImVec2(bounds.Max.x - line, bounds.Min.y), bounds.GetHeight(), line, border);
    }
}

Insets LinearContainer::contentInsets(RenderContext& context) const {
    const float scale = context.scale();
    return {m_padding.top * scale + (m_borders.top ? 1.0F : 0.0F), m_padding.right * scale + (m_borders.right ? 1.0F : 0.0F), m_padding.bottom * scale + (m_borders.bottom ? 1.0F : 0.0F), m_padding.left * scale + (m_borders.left ? 1.0F : 0.0F)};
}

ImVec2 LinearContainer::measureVertical(RenderContext& context, float availableWidth) {
    const Insets insets = contentInsets(context);
    const float inner = std::max(0.0F, availableWidth - insets.horizontal());
    const auto children = visibleChildren();
    float width = 0.0F;
    float height = 0.0F;

    for (auto* child : children) {
        const ImVec2 size = child->measure(context, inner);
        width = std::max(width, size.x);
        height += size.y;
    }

    if (children.size() > 1) {
        height += m_spacing * context.scale() * static_cast<float>(children.size() - 1);
    }

    return {width + insets.horizontal(), height + insets.vertical()};
}

ImVec2 LinearContainer::measureHorizontal(RenderContext& context, float availableWidth) {
    const Insets insets = contentInsets(context);
    const float inner = std::max(0.0F, availableWidth - insets.horizontal());
    const auto children = fitting(context, inner);
    const auto widths = horizontalWidths(context, children, inner);
    float height = 0.0F;

    for (std::size_t index = 0; index < children.size(); ++index) {
        height = std::max(height, children[index]->measure(context, widths[index]).y);
    }

    const float width = std::accumulate(widths.begin(), widths.end(), 0.0F) + (children.size() > 1 ? m_spacing * context.scale() * static_cast<float>(children.size() - 1) : 0.0F);
    return {width + insets.horizontal(), height + insets.vertical()};
}

void LinearContainer::renderVertical(RenderContext& context, const ImRect& content) {
    const auto children = visibleChildren();
    const float spacing = m_spacing * context.scale();
    std::vector<ImVec2> sizes;
    float total = children.size() > 1 ? spacing * static_cast<float>(children.size() - 1) : 0.0F;
    float growth = 0.0F;

    for (auto* child : children) {
        sizes.push_back(child->measure(context, content.GetWidth()));
        total += sizes.back().y;
        growth += child->common().grow;
    }

    // What the fixed children leave is shared by the growing ones, which also give back what the column lacks, and only a column that grows nothing justifies its children.
    const float free = content.GetHeight() - total;
    float y = content.Min.y;
    float gap = spacing;

    if (growth <= 0.0F && free > 0.0F) {
        if (m_justify == Justify::Center) {
            y += free / 2.0F;
        } else if (m_justify == Justify::End) {
            y += free;
        } else if (m_justify == Justify::SpaceBetween && children.size() > 1) {
            gap += free / static_cast<float>(children.size() - 1);
        }
    }

    const auto heights = verticalHeights(context, children, sizes, free);

    // A stretched child still keeps within its own bounds, which the width of the column caps like any other.
    for (std::size_t index = 0; index < children.size(); ++index) {
        Component* child = children[index];
        const Alignment alignment = child->columnAlignment();
        const float width = std::min(alignment == Alignment::Stretch ? child->boundedWidth(content.GetWidth(), context.scale()) : sizes[index].x, content.GetWidth());
        const float x = content.Min.x + alignedOffset(alignment, content.GetWidth(), width);
        child->draw(context, snapped(x, y, width, heights[index]));
        y += heights[index] + gap;
    }
}

void LinearContainer::renderHorizontal(RenderContext& context, const ImRect& content) {
    const auto children = fitting(context, content.GetWidth());
    const auto widths = horizontalWidths(context, children, content.GetWidth());
    const float spacing = m_spacing * context.scale();
    const float used = std::accumulate(widths.begin(), widths.end(), 0.0F) + (children.size() > 1 ? spacing * static_cast<float>(children.size() - 1) : 0.0F);
    const float free = content.GetWidth() - used;
    // clang-format off
    const bool grows = std::ranges::any_of(children, [](const Component* child) { return child->common().grow > 0.0F; });
    // clang-format on
    float x = content.Min.x;
    float gap = spacing;

    if (!grows && free > 0.0F) {
        if (m_justify == Justify::Center) {
            x += free / 2.0F;
        } else if (m_justify == Justify::End) {
            x += free;
        } else if (m_justify == Justify::SpaceBetween && children.size() > 1) {
            gap += free / static_cast<float>(children.size() - 1);
        }
    }

    for (std::size_t index = 0; index < children.size(); ++index) {
        Component* child = children[index];
        const Alignment alignment = child->rowAlignment();
        const float measured = child->measure(context, widths[index]).y;
        const float height = std::min(alignment == Alignment::Stretch ? child->boundedHeight(content.GetHeight(), context.scale()) : measured, content.GetHeight());
        const float y = content.Min.y + alignedOffset(alignment, content.GetHeight(), height);
        child->draw(context, snapped(x, y, widths[index], height));
        x += widths[index] + gap;
    }
}

// A row without room for every child at its content width and every growing child at its minimum hides its collapsible children, the highest collapse first, until the rest fits.
std::vector<Component*> LinearContainer::fitting(RenderContext& context, float available) {
    auto children = visibleChildren();

    while (!children.empty()) {
        float needed = children.size() > 1 ? m_spacing * context.scale() * static_cast<float>(children.size() - 1) : 0.0F;

        for (Component* child : children) {
            needed += child->common().grow > 0.0F ? child->boundedWidth(0.0F, context.scale()) : child->measure(context, available).x;
        }

        // clang-format off
        const auto hidden = std::ranges::max_element(children, {}, [](const Component* child) { return child->common().collapse; });
        // clang-format on

        if (needed <= available || (*hidden)->common().collapse == 0) {
            break;
        }

        children.erase(hidden);
    }

    return children;
}

// Children that do not grow keep the width their content asks for, and the growing ones share what is left by their factors.
std::vector<float> LinearContainer::horizontalWidths(RenderContext& context, const std::vector<Component*>& children, float available) {
    const float spacing = children.size() > 1 ? m_spacing * context.scale() * static_cast<float>(children.size() - 1) : 0.0F;
    std::vector<float> widths(children.size(), 0.0F);
    float fixed = 0.0F;
    float growth = 0.0F;

    for (std::size_t index = 0; index < children.size(); ++index) {
        if (children[index]->common().grow > 0.0F) {
            growth += children[index]->common().grow;
            continue;
        }

        widths[index] = children[index]->measure(context, std::max(0.0F, available - fixed)).x;
        fixed += widths[index];
    }

    // Growing children share what the fixed ones leave by their factors, a child held at its minimum takes what it needs from the share of the others, and room one cannot take past its maximum stays free.
    float left = std::max(0.0F, available - fixed - spacing);
    std::vector<bool> sharing(children.size(), false);

    for (std::size_t index = 0; index < children.size(); ++index) {
        sharing[index] = children[index]->common().grow > 0.0F;
    }

    for (bool held = growth > 0.0F; held;) {
        held = false;
        float factors = 0.0F;

        for (std::size_t index = 0; index < children.size(); ++index) {
            factors += sharing[index] ? children[index]->common().grow : 0.0F;
        }

        const float share = factors > 0.0F ? std::max(0.0F, left) / factors : 0.0F;

        for (std::size_t index = 0; index < children.size(); ++index) {
            if (!sharing[index]) {
                continue;
            }

            widths[index] = children[index]->boundedWidth(share * children[index]->common().grow, context.scale());
            held = held || widths[index] > share * children[index]->common().grow;
        }

        for (std::size_t index = 0; held && index < children.size(); ++index) {
            if (sharing[index] && widths[index] > share * children[index]->common().grow) {
                sharing[index] = false;
                left -= widths[index];
            }
        }
    }

    return widths;
}

// Growing children share the free height by their factors, and a shortfall one of them cannot give back past its minimum passes to the others, while room one cannot take past its maximum stays free as in a row.
std::vector<float> LinearContainer::verticalHeights(const RenderContext& context, const std::vector<Component*>& children, const std::vector<ImVec2>& sizes, float free) {
    std::vector<float> heights;
    std::vector<bool> sharing;

    for (std::size_t index = 0; index < children.size(); ++index) {
        heights.push_back(sizes[index].y);
        sharing.push_back(children[index]->common().grow > 0.0F);
    }

    float left = free;
    bool again = true;

    while (again) {
        float growth = 0.0F;

        for (std::size_t index = 0; index < children.size(); ++index) {
            growth += sharing[index] ? children[index]->common().grow : 0.0F;
        }

        if (growth <= 0.0F) {
            break;
        }

        again = false;
        float given = 0.0F;

        for (std::size_t index = 0; index < children.size(); ++index) {
            if (!sharing[index]) {
                continue;
            }

            const float wanted = heights[index] + left * children[index]->common().grow / growth;
            const float bounded = children[index]->boundedHeight(wanted, context.scale());
            given += bounded - heights[index];
            heights[index] = bounded;

            if (bounded != wanted) {
                sharing[index] = false;
                again = left < 0.0F;
            }
        }

        left -= given;
    }

    return heights;
}

} // namespace workpane::ui
