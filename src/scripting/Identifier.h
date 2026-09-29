#pragma once

#include <string_view>

namespace workpane::scripting {

// The grammar every identifier of a plugin follows, from the plugin itself to its destinations, sections and documents.
class Identifier final {
  public:
    [[nodiscard]] static bool valid(std::string_view identifier);
};

} // namespace workpane::scripting
