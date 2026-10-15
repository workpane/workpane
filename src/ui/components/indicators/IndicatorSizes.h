#pragma once

#include "json/ObjectReader.h"

#include <string_view>

namespace workpane::ui {

// Reads the size an indicator declares, bounded so a plugin cannot ask for a shape larger than any surface shows.
class IndicatorSizes final {
  public:
    static void read(json::ObjectReader& reader, std::string_view key, float& out);

  private:
    static constexpr double largest{256.0};
};

} // namespace workpane::ui
