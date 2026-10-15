#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/components/choices/ChoiceOption.h"

#include <string_view>
#include <vector>

namespace workpane::ui {

class ChoiceOptions final {
  public:
    [[nodiscard]] static Result<std::vector<ChoiceOption>> parse(const json::Json& options, std::string_view context);
};

} // namespace workpane::ui
