#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/model/TextValue.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace workpane::ui {

// What a component offers to drag: the kind a target must accept, the value the target receives and the label carried under the pointer.
struct DragValue final {
    static constexpr std::size_t maximumKindLength{64};

    [[nodiscard]] static Result<DragValue> parse(const json::Json& object, const std::string& context);
    [[nodiscard]] static bool validKind(std::string_view kind);

    std::string kind;
    json::Json value;
    TextValue label;
};

} // namespace workpane::ui
