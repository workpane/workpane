#include "ui/model/DragValue.h"

#include <algorithm>
#include <utility>

namespace workpane::ui {

Result<DragValue> DragValue::parse(const json::Json& object, const std::string& context) {
    DragValue drag;
    const json::Json* value = &json::ObjectReader::absent();
    const json::Json* label = &json::ObjectReader::absent();
    json::ObjectReader reader(object, context);
    reader.read("kind", drag.kind).readAny("value", value).readAny("label", label);

    if (auto finished = reader.finish(); !finished.hasValue()) {
        return Result<DragValue>::failure(finished.error());
    }

    if (!validKind(drag.kind)) {
        return Result<DragValue>::failure({"ui_drag_kind_invalid", "A drag kind is a dotted name of lowercase letters, numbers and hyphens", context + ".kind=" + drag.kind});
    }

    drag.value = *value;
    auto parsed = TextValue::parse(*label, context + ".label");

    if (!parsed.hasValue()) {
        return Result<DragValue>::failure(parsed.error());
    }

    drag.label = std::move(parsed.value());

    return Result<DragValue>::success(std::move(drag));
}

// A kind is a dotted name such as `terminal.session`, so a target names exactly the values it knows how to receive.
bool DragValue::validKind(std::string_view kind) {
    // clang-format off
    const auto allowed = [](char character) { return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '-' || character == '.'; };
    // clang-format on

    return !kind.empty() && kind.size() <= maximumKindLength && kind.front() != '.' && kind.back() != '.' && std::ranges::all_of(kind, allowed);
}

} // namespace workpane::ui
