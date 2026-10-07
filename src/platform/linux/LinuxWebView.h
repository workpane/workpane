#pragma once

#include "Result.h"
#include "platform/TrackedWebView.h"
#include "platform/posix/WebViewEngine.h"

#include <X11/Xlib.h>
#include <gtk/gtk.h>
#include <imgui_internal.h>
#include <webkit2/webkit2.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::platform {

class WebViewTracker;

// A page of WebKitGTK lives in a GTK plug created as a child of the product window, and the product speaks the few embedding messages that give it the keyboard.
// The engine builds the page of a view, and a window a page opens is a page related to its opener in a plug of its own.
class LinuxWebView final : public TrackedWebView {
  public:
    LinuxWebView(WebViewTracker& tracker, GtkWidget* plug, WebKitWebView* browser, std::unique_ptr<WebViewEngine> engine, Window parent, std::filesystem::path downloads);
    ~LinuxWebView() override;

    LinuxWebView(const LinuxWebView&) = delete;
    LinuxWebView& operator=(const LinuxWebView&) = delete;

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
    struct Download;

    static constexpr long xembedWindowActivate{1};
    static constexpr long xembedFocusIn{4};
    static constexpr long xembedFocusCurrent{0};
    static constexpr const char* handlerName{"workpane"};
    static constexpr const char* libraryHandlerName{"__webview__"};

    static gboolean pressed(GtkWidget* widget, GdkEvent* event, gpointer view);
    static void notified(GObject* object, GParamSpec* property, gpointer view);
    static void travelled(WebKitBackForwardList* list, WebKitBackForwardListItem* added, gpointer removed, gpointer view);
    static void posted(WebKitUserContentManager* manager, WebKitJavascriptResult* result, gpointer view);
    static GtkWidget* created(WebKitWebView* browser, WebKitNavigationAction* action, gpointer view);
    static void closed(WebKitWebView* browser, gpointer view);
    static gboolean decided(WebKitWebView* browser, WebKitPolicyDecision* decision, WebKitPolicyDecisionType type, gpointer view);
    static void started(WebKitWebContext* context, WebKitDownload* download, gpointer view);
    static gboolean destined(WebKitDownload* download, gchar* suggested, gpointer view);
    static void interrupted(WebKitDownload* download, GError* error, gpointer view);
    static void completed(WebKitDownload* download, gpointer view);
    static gboolean permitted(WebKitWebView* browser, WebKitPermissionRequest* request, gpointer view);
    [[nodiscard]] static std::string text(const gchar* value);

    [[nodiscard]] GtkWidget* openPopup();
    [[nodiscard]] Download* active(WebKitDownload* download);
    void focus();
    void refresh();

    GtkWidget* m_plug;
    WebKitWebView* m_browser;
    std::unique_ptr<WebViewEngine> m_engine;
    Window m_parent;
    WebKitUserContentManager* m_messages{nullptr};
    WebKitWebContext* m_context{nullptr};
    std::filesystem::path m_downloads;
    std::vector<Download> m_active;
};

} // namespace workpane::platform
