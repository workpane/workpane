#pragma once

#include "Result.h"
#include "ui/NativeWebView.h"

#include <imgui_internal.h>

#include <memory>
#include <vector>

namespace workpane::ui {

// Creates the native views of the running platform, so components never name a window system.
// The frame loop gives the native event handling a turn on every iteration and learns whether a view reported something to draw.
// Right before the loop sleeps, a toolkit whose events only run inside that turn has them watched meanwhile, so one of its events wakes the loop, and the end of each frame hides the views nobody placed in it.
// The end of each frame also names the windows the product drew above the views, which the views leave uncovered.
class NativeViewHost {
  public:
    virtual ~NativeViewHost() = default;

    [[nodiscard]] virtual Result<std::unique_ptr<NativeWebView>> createWebView() = 0;
    [[nodiscard]] virtual bool pump() = 0;
    virtual void watch() = 0;
    virtual void endFrame(const std::vector<ImRect>& floating) = 0;
};

} // namespace workpane::ui
