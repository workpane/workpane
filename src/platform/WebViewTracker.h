#pragma once

#include "platform/TrackedWebView.h"

#include <vector>

namespace workpane::platform {

// The web views of the product window, which a frame hides when their components were not drawn and which decide where the keyboard goes.
class WebViewTracker final {
  public:
    void track(TrackedWebView* view);
    void forget(TrackedWebView* view);
    void endFrame(const std::vector<ImRect>& floating);
    [[nodiscard]] bool clickedOutside() const;
    void reportActivity();
    [[nodiscard]] bool takeActivity();

  private:
    std::vector<TrackedWebView*> m_views;
    bool m_activity{false};
};

} // namespace workpane::platform
