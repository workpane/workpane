#pragma once

#include "ui/IconCatalog.h"
#include "ui/shell/NavigationShortcut.h"

#include <cstdint>
#include <string>
#include <vector>

namespace workpane::ui {

enum class NavigationPlacement { Primary, Secondary };

// One destination of the mode bar, declared by the plugin that owns the view it opens.
struct NavigationItem final {
    std::string plugin;
    std::string id;
    std::string titleKey;
    Icon icon{Icon::Workspace};
    NavigationPlacement placement{NavigationPlacement::Primary};
    std::int64_t order{0};
    bool preload{false};
    std::vector<NavigationShortcut> shortcuts;

    [[nodiscard]] std::string destination() const;
    [[nodiscard]] std::string surface() const;
};

} // namespace workpane::ui
