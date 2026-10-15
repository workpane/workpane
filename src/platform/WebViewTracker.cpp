#include "platform/WebViewTracker.h"

#include "platform/PlatformWindow.h"

#include <algorithm>
#include <utility>

namespace workpane::platform {

void WebViewTracker::track(TrackedWebView* view) {
    m_views.push_back(view);
}

void WebViewTracker::forget(TrackedWebView* view) {
    std::erase(m_views, view);
}

void WebViewTracker::endFrame(const std::vector<ImRect>& floating) {
    for (auto* view : m_views) {
        view->hideUnplaced();
        view->cover(floating);
    }
}

// A press on the product outside every visible web view means the reader left the page, so the keyboard returns to the product window.
bool WebViewTracker::clickedOutside() const {
    if (m_views.empty() || (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
        return false;
    }

    const ImVec2 pointer = ImGui::GetIO().MousePos;
    // clang-format off
    return std::ranges::none_of(m_views, [pointer](const TrackedWebView* view) { return view->contains(pointer); });
    // clang-format on
}

// Wakes a frame loop that may be sleeping, because a web view reports its loads while nothing else asks for a frame.
void WebViewTracker::reportActivity() {
    m_activity = true;
    PlatformWindow::wake();
}

bool WebViewTracker::takeActivity() {
    return std::exchange(m_activity, false);
}

} // namespace workpane::platform
