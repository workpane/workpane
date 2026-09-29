#include "ui/components/indicators/IndicatorSizes.h"

namespace workpane::ui {

void IndicatorSizes::read(json::ObjectReader& reader, std::string_view key, float& out) {
    double size = out;
    reader.readNumber(key, size, 1.0, largest, json::Presence::Optional);
    out = static_cast<float>(size);
}

} // namespace workpane::ui
