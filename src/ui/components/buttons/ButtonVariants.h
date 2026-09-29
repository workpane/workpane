#pragma once

#include "json/ObjectReader.h"
#include "ui/Widgets.h"

namespace workpane::ui {

// Reads the variant a button component declares, which chooses how the shared button is painted.
class ButtonVariants final {
  public:
    static void read(json::ObjectReader& reader, ButtonVariant& variant);
};

} // namespace workpane::ui
