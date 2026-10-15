#pragma once

#include "platform/DialogService.h"
#include "ui/NativeViewHost.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <functional>
#include <memory>

namespace workpane::ui {
class Theme;
}

namespace workpane::app {

// What the window lends the product once it exists: the font atlas, the platform dialogs and native views, the ways to wake and restyle it and the displays it sees.
struct ProductInterface final {
    ImFontAtlas& fonts;
    std::unique_ptr<platform::DialogService> dialogs;
    std::unique_ptr<ui::NativeViewHost> nativeViews;
    float scale{1.0F};
    std::function<void()> wake;
    std::function<void(const ui::Theme& theme)> themeApplied;
    std::function<nlohmann::json()> displays;
};

} // namespace workpane::app
