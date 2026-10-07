#include "ui/Painter.h"

#include "ui/WidgetHelper.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace workpane::ui {

void Painter::indicator(ImDrawList& list, ImVec2 center, float diameter, Color color) {
    list.AddCircleFilled(center, diameter / 2.0F, WidgetHelper::ink(color));
}

void Painter::closeCircle(ImDrawList& list, ImVec2 center, float diameter, Color circle, Color cross, bool emphasized) {
    const float radius = diameter / 2.0F;
    list.AddCircleFilled(center, radius, WidgetHelper::ink(circle.withAlpha(emphasized ? 1.0F : 0.72F)));

    // The strokes are paths rather than lines, because ImGui moves a line half a point toward the bottom right while the circle stays where it is.
    const float reach = radius - diameter * closeCrossInset;
    list.PathLineTo(ImVec2(center.x - reach, center.y - reach));
    list.PathLineTo(ImVec2(center.x + reach, center.y + reach));
    list.PathStroke(WidgetHelper::ink(cross), closeCrossThickness, ImDrawFlags_None);
    list.PathLineTo(ImVec2(center.x + reach, center.y - reach));
    list.PathLineTo(ImVec2(center.x - reach, center.y + reach));
    list.PathStroke(WidgetHelper::ink(cross), closeCrossThickness, ImDrawFlags_None);
}

void Painter::chevron(ImDrawList& list, ImVec2 center, float width, ChevronDirection direction, Color color) {
    const float half = width / 2.0F;
    const float depth = width / 4.0F;

    switch (direction) {
    case ChevronDirection::Down:
        list.PathLineTo(ImVec2(center.x - half, center.y - depth));
        list.PathLineTo(ImVec2(center.x, center.y + depth));
        list.PathLineTo(ImVec2(center.x + half, center.y - depth));
        break;
    case ChevronDirection::Right:
        list.PathLineTo(ImVec2(center.x - depth, center.y - half));
        list.PathLineTo(ImVec2(center.x + depth, center.y));
        list.PathLineTo(ImVec2(center.x - depth, center.y + half));
        break;
    }

    list.PathStroke(WidgetHelper::ink(color), chevronThickness, ImDrawFlags_None);
}

void Painter::checkMark(ImDrawList& list, ImVec2 topLeft, float size, Color color) {
    list.PathLineTo(ImVec2(topLeft.x + size * 0.22F, topLeft.y + size * 0.52F));
    list.PathLineTo(ImVec2(topLeft.x + size * 0.42F, topLeft.y + size * 0.72F));
    list.PathLineTo(ImVec2(topLeft.x + size * 0.78F, topLeft.y + size * 0.30F));
    list.PathStroke(WidgetHelper::ink(color), std::max(1.6F, size * 0.12F), ImDrawFlags_None);
}

// The busy ring reads its angle from the clock, so a frame the interface was too busy to draw is skipped instead of stuttering.
void Painter::busyRing(ImDrawList& list, ImVec2 center, float radius, double seconds, Color color) {
    const double turn = std::fmod(seconds, busyTurnSeconds) / busyTurnSeconds;
    const auto start = static_cast<float>(turn * 2.0 * std::numbers::pi);
    const float sweep = busySweepDegrees * std::numbers::pi_v<float> / 180.0F;
    list.PathArcTo(center, radius, start, start + sweep, 32);
    list.PathStroke(WidgetHelper::ink(color), 2.0F, ImDrawFlags_None);
}

void Painter::horizontalDivider(ImDrawList& list, ImVec2 from, float width, float thickness, Color color) {
    const ImVec2 origin(std::floor(from.x), std::floor(from.y));
    list.AddRectFilled(origin, ImVec2(origin.x + width, origin.y + pixels(thickness)), WidgetHelper::ink(color));
}

void Painter::verticalDivider(ImDrawList& list, ImVec2 from, float height, float thickness, Color color) {
    const ImVec2 origin(std::floor(from.x), std::floor(from.y));
    list.AddRectFilled(origin, ImVec2(origin.x + pixels(thickness), origin.y + height), WidgetHelper::ink(color));
}

// A border is drawn inside the rectangle it outlines, so it never spills over what surrounds the rectangle.
void Painter::rectBorder(ImDrawList& list, ImVec2 minimum, ImVec2 maximum, float rounding, float thickness, Color color) {
    const float width = pixels(thickness);
    const ImVec2 inset((width - 1.0F) / 2.0F, (width - 1.0F) / 2.0F);
    list.AddRect(minimum + inset, maximum - inset, WidgetHelper::ink(color), rounding, width);
}

// A line keeps whole pixels, so one point is one pixel at the first scale, two at the second and never a blurred half.
float Painter::pixels(float thickness) {
    return std::max(1.0F, std::round(thickness));
}

} // namespace workpane::ui
