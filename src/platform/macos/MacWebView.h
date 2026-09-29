#pragma once

#include "Result.h"
#include "platform/TrackedWebView.h"
#include "platform/posix/WebViewEngine.h"

#include <imgui_internal.h>

#import <Cocoa/Cocoa.h>

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

@class WKDownload;
@class WKWebView;
@class WKWebViewConfiguration;

namespace workpane::platform {

class WebViewTracker;

// A web page of WebKit moved into the product window as a subview, either built by the engine inside a holder window that is never shown or opened by another page.
// The pages of every view post to one messenger, ask one delegate for windows and one navigator for downloads, and each of them finds the view of a page through the page itself.
class MacWebView final : public TrackedWebView {
  public:
    MacWebView(WebViewTracker& tracker, NSView* host, WKWebView* webView, std::unique_ptr<WebViewEngine> engine, NSWindow* holder, std::filesystem::path downloads);
    ~MacWebView() override;

    MacWebView(const MacWebView&) = delete;
    MacWebView& operator=(const MacWebView&) = delete;

    [[nodiscard]] Result<void> navigate(std::string_view url) override;
    [[nodiscard]] Result<void> setHtml(std::string_view html) override;
    [[nodiscard]] Result<void> evaluate(std::string_view script) override;
    void reload() override;
    void back() override;
    void forward() override;
    void stop() override;

  protected:
    void apply(const ImRect& bounds, bool visible) override;
    void cut(ImVec2 size, const std::vector<ImRect>& holes) override;

  private:
    static constexpr const char* observerName{"WorkpaneNavigationObserver"};
    static constexpr const char* frameName{"WorkpaneWebViewFrame"};
    static constexpr const char* delegateName{"WorkpaneWebViewDelegate"};
    static constexpr const char* messengerName{"WorkpanePageMessenger"};
    static constexpr const char* navigatorName{"WorkpaneWebViewNavigator"};
    static constexpr const char* libraryDelegateName{"WebviewWKUIDelegate"};
    static constexpr const char* handlerName{"workpane"};
    static constexpr const char* libraryHandlerName{"__webview__"};
    static constexpr char holesKey{0};
    static constexpr char viewKey{0};
    static constexpr char innerKey{0};
    static constexpr char pathKey{0};
    static constexpr std::array<const char*, 5> observedKeys{"URL", "title", "loading", "canGoBack", "canGoForward"};

    [[nodiscard]] static NSString* browserUserAgent();
    [[nodiscard]] static std::vector<CGRect> subtract(const std::vector<CGRect>& pieces, CGRect removed);
    [[nodiscard]] static Class observerClass();
    [[nodiscard]] static Class frameClass();
    [[nodiscard]] static Class delegateClass();
    [[nodiscard]] static Class messengerClass();
    [[nodiscard]] static Class navigatorClass();
    static void finishDownload(WKDownload* download, bool finished, NSString* message);
    [[nodiscard]] static MacWebView* owner(WKWebView* webView);

    [[nodiscard]] WKWebView* openPopup(WKWebViewConfiguration* configuration);
    void refresh();

    NSWindow* m_holder;
    NSView* m_host;
    WKWebView* m_webView;
    std::unique_ptr<WebViewEngine> m_engine;
    NSView* m_frame{nil};
    id m_observer{nil};
    id m_delegate{nil};
    id m_navigator{nil};
    std::filesystem::path m_downloads;
};

} // namespace workpane::platform
