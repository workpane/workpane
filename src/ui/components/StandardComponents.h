#pragma once

#include "ui/model/ComponentRegistry.h"

#include <string>

namespace workpane::ui {

// The complete set of kinds a plugin may declare, registered in one place so the registry and the documentation name the same list.
class StandardComponents final {
  public:
    static void registerAll(ComponentRegistry& registry);

  private:
    template <typename T> static void add(ComponentRegistry& registry, std::string kind);
};

} // namespace workpane::ui
