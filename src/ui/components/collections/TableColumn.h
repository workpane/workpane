#pragma once

#include "ui/WidgetHelper.h"
#include "ui/model/TextValue.h"

#include <string>

namespace workpane::ui {

enum class ColumnWidth { Content, Stretch, Fixed };

struct TableColumn final {
    std::string id;
    TextValue title;
    ColumnWidth width{ColumnWidth::Content};
    float fixed{0.0F};
    TextAlign align{TextAlign::Start};
};

} // namespace workpane::ui
