#pragma once

#include "Error.h"
#include "ui/NativeWebView.h"
#include "ui/WebDownload.h"
#include "ui/WebNavigation.h"

#include <imgui_internal.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::platform {

class WebViewTracker;

// A native web view that follows the component drawing it, and hides itself in every frame that component was not drawn.
// Each platform moves and shows its own native widget, drives its page and its history, opens the windows its pages ask for and reports where its page is.
// Every page runs the page script, which posts the address the reader opens in a background tab and the icon the page draws.
class TrackedWebView : public ui::NativeWebView {
  public:
    static constexpr const char* scriptWorld{"workpane"};
    static constexpr const char* pageScript{"(function () { if (window.top !== window || window.__workpaneReady) { return; } window.__workpaneReady = true; var handlers = window.webkit && window.webkit.messageHandlers; var post = handlers && handlers.workpane ? function (message) { handlers.workpane.postMessage(message); } : function (message) { window.chrome.webview.postMessage(message); }; var opened = function (event) { var anchor = event.target instanceof Element ? event.target.closest('a[href]') : null; if (anchor === null || !/^https?:/.test(anchor.href) || !(event.button === 1 || (event.button === 0 && (event.metaKey || event.ctrlKey)))) { return; } event.preventDefault(); event.stopPropagation(); post({ kind: 'open', url: anchor.href }); }; document.addEventListener('click', opened, true); document.addEventListener('auxclick', opened, true); var icon = function () { var link = document.querySelector('link[rel~=\"icon\"]'); var image = new Image(); image.crossOrigin = 'anonymous'; image.onload = function () { var canvas = document.createElement('canvas'); canvas.width = 32; canvas.height = 32; canvas.getContext('2d').drawImage(image, 0, 0, 32, 32); try { post({ kind: 'icon', data: canvas.toDataURL('image/png') }); } catch (error) { return; } }; image.src = link !== null ? link.href : location.origin + '/favicon.ico'; }; if (document.readyState === 'complete') { icon(); } else { window.addEventListener('load', icon); } })();"};

    explicit TrackedWebView(WebViewTracker& tracker);
    ~TrackedWebView() override;

    TrackedWebView(const TrackedWebView&) = delete;
    TrackedWebView& operator=(const TrackedWebView&) = delete;

    void place(const ImRect& bounds, bool visible) final;
    void setNavigationHandler(NavigationHandler handler) final;
    void setOpenHandler(OpenHandler handler) final;
    void setPopupHandler(PopupHandler handler) final;
    void setCloseHandler(CloseHandler handler) final;
    void setDownloadHandler(DownloadHandler handler) final;
    void setPermissionHandler(PermissionHandler handler) final;
    void setFailureHandler(FailureHandler handler) final;
    void answerPermission(std::uint64_t request, bool allowed) final;

    void hideUnplaced();
    void cover(const std::vector<ImRect>& floating);
    [[nodiscard]] bool contains(ImVec2 point) const;

  protected:
    virtual void apply(const ImRect& bounds, bool visible) = 0;
    virtual void cut(ImVec2 size, const std::vector<ImRect>& holes) = 0;
    [[nodiscard]] WebViewTracker& tracker();
    [[nodiscard]] bool opensPopups() const;
    void report(ui::WebNavigation navigation);
    void receive(std::string_view message);
    void offer(std::unique_ptr<TrackedWebView> popup);
    void closeRequested();
    void downloaded(ui::WebDownload download);
    void ask(std::string_view url, bool camera, bool microphone, std::function<void(bool allowed)> answer);
    void refuseQuestions();
    void failed(Error error);

  private:
    static constexpr std::size_t largestIcon{65536};
    static constexpr int opensPerSecond{4};
    static constexpr std::string_view iconPrefix{"data:image/png;base64,"};

    [[nodiscard]] static bool same(const std::vector<ImRect>& first, const std::vector<ImRect>& second);
    [[nodiscard]] static std::string_view origin(std::string_view url);

    void deliver();

    WebViewTracker& m_tracker;
    NavigationHandler m_navigationHandler;
    OpenHandler m_openHandler;
    PopupHandler m_popupHandler;
    CloseHandler m_closeHandler;
    DownloadHandler m_downloadHandler;
    PermissionHandler m_permissionHandler;
    FailureHandler m_failureHandler;
    std::map<std::uint64_t, std::function<void(bool allowed)>> m_questions;
    std::uint64_t m_nextQuestion{1};
    ui::WebNavigation m_navigation;
    std::string m_icon;
    std::optional<Error> m_failure;
    std::chrono::steady_clock::time_point m_openSecond;
    int m_opens{0};
    ImRect m_bounds;
    ImRect m_cutBounds;
    std::vector<ImRect> m_holes;
    bool m_visible{false};
    bool m_placed{false};
};

} // namespace workpane::platform
