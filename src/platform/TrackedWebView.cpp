#include "platform/TrackedWebView.h"

#include "platform/UrlPolicy.h"
#include "platform/WebViewTracker.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace workpane::platform {

TrackedWebView::TrackedWebView(WebViewTracker& tracker) : m_tracker(tracker) {
    m_tracker.track(this);
}

TrackedWebView::~TrackedWebView() {
    m_tracker.forget(this);
}

void TrackedWebView::place(const ImRect& bounds, bool visible) {
    m_placed = true;

    if (bounds.Min.x == m_bounds.Min.x && bounds.Min.y == m_bounds.Min.y && bounds.Max.x == m_bounds.Max.x && bounds.Max.y == m_bounds.Max.y && visible == m_visible) {
        return;
    }

    apply(bounds, visible);
    m_bounds = bounds;
    m_visible = visible;
}

// A view adopted after its page moved on, such as a window a page opened, reports where it stands to its new owner at once.
void TrackedWebView::setNavigationHandler(NavigationHandler handler) {
    m_navigationHandler = std::move(handler);

    if (!m_navigation.url.empty()) {
        deliver();
    }
}

void TrackedWebView::setOpenHandler(OpenHandler handler) {
    m_openHandler = std::move(handler);
}

void TrackedWebView::setPopupHandler(PopupHandler handler) {
    m_popupHandler = std::move(handler);
}

void TrackedWebView::setCloseHandler(CloseHandler handler) {
    m_closeHandler = std::move(handler);
}

void TrackedWebView::setDownloadHandler(DownloadHandler handler) {
    m_downloadHandler = std::move(handler);
}

void TrackedWebView::setPermissionHandler(PermissionHandler handler) {
    m_permissionHandler = std::move(handler);
}

// A view that could not be built before its owner listened reports it as soon as the owner does.
void TrackedWebView::setFailureHandler(FailureHandler handler) {
    m_failureHandler = std::move(handler);

    if (m_failure.has_value() && m_failureHandler) {
        m_failureHandler(*m_failure);
    }
}

// The answer reaches the page that asked once, and an answer to a question the view no longer holds changes nothing.
void TrackedWebView::answerPermission(std::uint64_t request, bool allowed) {
    const auto found = m_questions.find(request);

    if (found == m_questions.end()) {
        return;
    }

    const auto answer = std::move(found->second);
    m_questions.erase(found);
    answer(allowed);
}

void TrackedWebView::hideUnplaced() {
    if (!m_placed && m_visible) {
        apply(m_bounds, false);
        m_visible = false;
    }

    m_placed = false;
}

// The windows the product drew over the view, such as a tooltip or a menu, are cut out of it, so they show and take the pointer where they overlap the page.
// A window system that clips the drawing of the product under the view shows a hole only once the product draws again, so every new cut asks for a frame.
void TrackedWebView::cover(const std::vector<ImRect>& floating) {
    std::vector<ImRect> holes;

    for (const ImRect& window : floating) {
        if (m_visible && window.Overlaps(m_bounds)) {
            ImRect hole = window;
            hole.ClipWithFull(m_bounds);
            holes.emplace_back(hole.Min - m_bounds.Min, hole.Max - m_bounds.Min);
        }
    }

    if (m_cutBounds.GetSize() == m_bounds.GetSize() && same(holes, m_holes)) {
        return;
    }

    cut(m_bounds.GetSize(), holes);
    m_holes = std::move(holes);
    m_cutBounds = m_bounds;
    m_tracker.reportActivity();
}

// A point inside a window cut out of the view belongs to the product rather than to the page.
bool TrackedWebView::contains(ImVec2 point) const {
    if (!m_visible || !m_bounds.Contains(point)) {
        return false;
    }

    const ImVec2 local = point - m_bounds.Min;
    // clang-format off
    return std::ranges::none_of(m_holes, [local](const ImRect& hole) { return hole.Contains(local); });
    // clang-format on
}

bool TrackedWebView::same(const std::vector<ImRect>& first, const std::vector<ImRect>& second) {
    // clang-format off
    return std::ranges::equal(first, second, [](const ImRect& left, const ImRect& right) { return left.Min == right.Min && left.Max == right.Max; });
    // clang-format on
}

WebViewTracker& TrackedWebView::tracker() {
    return m_tracker;
}

// A page gets a window of its own only when the owner of the view takes such windows.
bool TrackedWebView::opensPopups() const {
    return static_cast<bool>(m_popupHandler);
}

// Only a navigation that differs from the last one reported reaches the handler, because the platform repeats itself while a page loads.
// The icon belongs to the site that drew it, so a page of another site starts without one until it draws its own.
void TrackedWebView::report(ui::WebNavigation navigation) {
    if (origin(navigation.url) != origin(m_navigation.url)) {
        m_icon.clear();
    }

    navigation.icon = m_icon;

    if (navigation == m_navigation) {
        return;
    }

    m_navigation = std::move(navigation);
    deliver();
}

// The page script posts JSON objects, whose kind and values are checked here, and a burst of opens beyond what a reader clicks is dropped, since a page that reaches the channel could post them without end.
void TrackedWebView::receive(std::string_view message) {
    const auto parsed = nlohmann::json::parse(message, nullptr, false);

    if (!parsed.is_object() || !parsed.contains("kind") || !parsed["kind"].is_string()) {
        return;
    }

    const std::string& kind = parsed["kind"].get_ref<const std::string&>();

    if (kind == "open" && parsed.contains("url") && parsed["url"].is_string() && UrlPolicy::allowed(parsed["url"].get<std::string>()) && m_openHandler) {
        const auto now = std::chrono::steady_clock::now();
        m_opens = now - m_openSecond < std::chrono::seconds(1) ? m_opens + 1 : 1;
        m_openSecond = m_opens == 1 ? now : m_openSecond;

        if (m_opens > opensPerSecond) {
            return;
        }

        m_openHandler(parsed["url"].get<std::string>(), true);
        m_tracker.reportActivity();
        return;
    }

    if (kind != "icon" || !parsed.contains("data") || !parsed["data"].is_string()) {
        return;
    }

    const std::string& icon = parsed["data"].get_ref<const std::string&>();

    if (!icon.starts_with(iconPrefix) || icon.size() > largestIcon || icon == m_icon) {
        return;
    }

    m_icon = icon;
    m_navigation.icon = m_icon;
    deliver();
}

// A window a page opened goes to the owner of the view, which hands it to the component that will show it.
void TrackedWebView::offer(std::unique_ptr<TrackedWebView> popup) {
    m_popupHandler(std::move(popup));
    m_tracker.reportActivity();
}

void TrackedWebView::closeRequested() {
    if (m_closeHandler) {
        m_closeHandler();
        m_tracker.reportActivity();
    }
}

// A download reaches the owner of the view once it finished or failed, and a view nobody owns yet keeps the file where it was saved.
void TrackedWebView::downloaded(ui::WebDownload download) {
    if (m_downloadHandler) {
        m_downloadHandler(std::move(download));
        m_tracker.reportActivity();
    }
}

// A page asking for the camera or the microphone waits for the owner of the view, and a view nobody owns refuses at once.
void TrackedWebView::ask(std::string_view url, bool camera, bool microphone, std::function<void(bool allowed)> answer) {
    if (!m_permissionHandler) {
        answer(false);
        return;
    }

    const std::uint64_t request = m_nextQuestion++;
    m_questions.emplace(request, std::move(answer));
    m_permissionHandler({request, std::string(origin(url)), camera, microphone});
    m_tracker.reportActivity();
}

// Every question still waiting is refused, which a view does before its page goes.
void TrackedWebView::refuseQuestions() {
    auto questions = std::exchange(m_questions, {});

    for (auto& [request, answer] : questions) {
        answer(false);
    }
}

// A view built in the background reports once that it could not be built, which its owner shows in its place.
void TrackedWebView::failed(Error error) {
    if (m_failure.has_value()) {
        return;
    }

    m_failure = std::move(error);

    if (m_failureHandler) {
        m_failureHandler(*m_failure);
        m_tracker.reportActivity();
    }
}

// The origin is the scheme and the authority of an address, which is where its icon comes from.
std::string_view TrackedWebView::origin(std::string_view url) {
    const std::size_t scheme = url.find("://");

    if (scheme == std::string_view::npos) {
        return url;
    }

    const std::size_t path = url.find_first_of("/?#", scheme + 3);
    return url.substr(0, path);
}

void TrackedWebView::deliver() {
    if (m_navigationHandler) {
        m_navigationHandler(m_navigation);
    }

    m_tracker.reportActivity();
}

} // namespace workpane::platform
