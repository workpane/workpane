#pragma once

#include <cstdint>
#include <string>

namespace workpane::ui {

enum class BandPlacement { Top, Bottom };

// One band across the whole window above or below everything else, declared by the plugin that owns the view it draws.
struct BandItem final {
    std::string plugin;
    std::string id;
    std::string titleKey;
    BandPlacement placement{BandPlacement::Bottom};
    std::int64_t order{0};
    std::int64_t height{0};

    [[nodiscard]] std::string surface() const;
};

} // namespace workpane::ui
