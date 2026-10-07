#pragma once

#include "ui/model/TextValue.h"

#include <string>

namespace workpane::ui {

struct ChoiceOption final {
    std::string value;
    TextValue text;
};

} // namespace workpane::ui
