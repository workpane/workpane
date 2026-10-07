#include "platform/linux/LinuxWebView.h"

#include "platform/DownloadTarget.h"

#include <X11/Xlib.h>
#include <X11/extensions/shape.h>
#include <gdk/gdkx.h>
#include <gtk/gtkx.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace workpane::platform {

// A download of this view, with the file it was given and whether it already failed, since the engine announces its end even after a failure.
struct LinuxWebView::Download final {
    WebKitDownload* download;
    std::filesystem::path path;
    bool failed{false};
};

LinuxWebView::LinuxWebView(WebViewTracker& tracker, GtkWidget* plug, WebKitWebView* browser, std::unique_ptr<WebViewEngine> engine, Window parent, std::filesystem::path downloads) : TrackedWebView(tracker), m_plug(plug), m_browser(browser), m_engine(std::move(engine)), m_parent(parent), m_downloads(std::move(downloads)) {
    gtk_widget_add_events(GTK_WIDGET(m_browser), GDK_BUTTON_PRESS_MASK);
    g_signal_connect(G_OBJECT(m_browser), "button-press-event", G_CALLBACK(&LinuxWebView::pressed), this);
    g_signal_connect(G_OBJECT(m_browser), "create", G_CALLBACK(&LinuxWebView::created), this);
    g_signal_connect(G_OBJECT(m_browser), "close", G_CALLBACK(&LinuxWebView::closed), this);

    // Every page has a content manager of its own, which runs the page script and hands its posts to this view.
    // The engine registers a handler and scripts of its own for functions the product never binds, so a built view drops them.
    // The handler and the page script live in a script world of their own, which no script of the page reaches.
    m_messages = webkit_web_view_get_user_content_manager(m_browser);

    if (m_engine != nullptr) {
        webkit_user_content_manager_unregister_script_message_handler(m_messages, libraryHandlerName);
        webkit_user_content_manager_remove_all_scripts(m_messages);
    }

    g_signal_connect(G_OBJECT(m_messages), "script-message-received::workpane", G_CALLBACK(&LinuxWebView::posted), this);
    webkit_user_content_manager_register_script_message_handler_in_world(m_messages, handlerName, scriptWorld);
    WebKitUserScript* script = webkit_user_script_new_for_world(pageScript, WEBKIT_USER_CONTENT_INJECT_TOP_FRAME, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END, scriptWorld, nullptr, nullptr);
    webkit_user_content_manager_add_script(m_messages, script);
    webkit_user_script_unref(script);

    // The page announces every change of its address, title, loading and history, and each one reads the whole state again.
    for (const char* signal : {"notify::uri", "notify::title", "notify::is-loading"}) {
        g_signal_connect(G_OBJECT(m_browser), signal, G_CALLBACK(&LinuxWebView::notified), this);
    }

    g_signal_connect(G_OBJECT(webkit_web_view_get_back_forward_list(m_browser)), "changed", G_CALLBACK(&LinuxWebView::travelled), this);

    // A response the page cannot show becomes a download, and the context every related page shares announces each download to all of their views.
    g_signal_connect(G_OBJECT(m_browser), "decide-policy", G_CALLBACK(&LinuxWebView::decided), this);
    m_context = webkit_web_view_get_context(m_browser);
    g_signal_connect(G_OBJECT(m_context), "download-started", G_CALLBACK(&LinuxWebView::started), this);

    // Pages may speak WebRTC wherever the WebKitGTK of the system carries it, which the engine leaves off, and a page asks the owner of its view before it captures the camera or the microphone.
    webkit_settings_set_enable_webrtc(webkit_web_view_get_settings(m_browser), TRUE);
    g_signal_connect(G_OBJECT(m_browser), "permission-request", G_CALLBACK(&LinuxWebView::permitted), this);
    gtk_widget_show_all(m_plug);
    gtk_widget_set_visible(m_plug, FALSE);
}

LinuxWebView::~LinuxWebView() {
    refuseQuestions();

    for (const Download& download : m_active) {
        g_signal_handlers_disconnect_by_data(G_OBJECT(download.download), this);
        g_object_unref(download.download);
    }

    g_signal_handlers_disconnect_by_data(G_OBJECT(m_context), this);
    g_signal_handlers_disconnect_by_data(G_OBJECT(webkit_web_view_get_back_forward_list(m_browser)), this);
    g_signal_handlers_disconnect_by_data(G_OBJECT(m_messages), this);
    g_signal_handlers_disconnect_by_data(G_OBJECT(m_browser), this);
    webkit_user_content_manager_unregister_script_message_handler_in_world(m_messages, handlerName, scriptWorld);
    m_engine.reset();
    gtk_widget_destroy(m_plug);
}

Result<void> LinuxWebView::navigate(std::string_view url) {
    webkit_web_view_load_uri(m_browser, std::string(url).c_str());

    return Result<void>::success();
}

Result<void> LinuxWebView::setHtml(std::string_view html) {
    webkit_web_view_load_html(m_browser, std::string(html).c_str(), nullptr);

    return Result<void>::success();
}

Result<void> LinuxWebView::evaluate(std::string_view script) {
    webkit_web_view_evaluate_javascript(m_browser, script.data(), static_cast<gssize>(script.size()), nullptr, nullptr, nullptr, nullptr, nullptr);

    return Result<void>::success();
}

void LinuxWebView::reload() {
    webkit_web_view_reload(m_browser);
}

void LinuxWebView::back() {
    webkit_web_view_go_back(m_browser);
}

void LinuxWebView::forward() {
    webkit_web_view_go_forward(m_browser);
}

void LinuxWebView::stop() {
    webkit_web_view_stop_loading(m_browser);
}

void LinuxWebView::apply(const ImRect& bounds, bool visible) {
    const int width = std::max(1, static_cast<int>(std::lround(bounds.GetWidth())));
    const int height = std::max(1, static_cast<int>(std::lround(bounds.GetHeight())));
    gtk_window_resize(GTK_WINDOW(m_plug), width, height);
    gtk_widget_set_visible(m_plug, visible ? TRUE : FALSE);
    GdkWindow* window = gtk_widget_get_window(m_plug);

    if (window == nullptr) {
        return;
    }

    // An embedding socket maps the window of its plug, and the product window is no socket, so the view maps and unmaps itself.
    gdk_window_move_resize(window, static_cast<int>(std::lround(bounds.Min.x)), static_cast<int>(std::lround(bounds.Min.y)), width, height);

    if (visible) {
        gdk_window_show(window);
        return;
    }

    gdk_window_hide(window);
}

// The plug window is shaped around the holes for drawing and for input alike, so the windows of the product show through and take the pointer there.
// The shape is set on the X window itself, because GDK never sends the shape of a plug that lives inside a window it does not know.
void LinuxWebView::cut(ImVec2 size, const std::vector<ImRect>& holes) {
    GdkWindow* window = gtk_widget_get_window(m_plug);

    if (window == nullptr) {
        return;
    }

    Display* display = GDK_WINDOW_XDISPLAY(window);
    const Window plug = GDK_WINDOW_XID(window);

    // A page given back its whole area draws again the part the holes had taken.
    if (holes.empty()) {
        XShapeCombineMask(display, plug, ShapeBounding, 0, 0, None, ShapeSet);
        XShapeCombineMask(display, plug, ShapeInput, 0, 0, None, ShapeSet);
        gtk_widget_queue_draw(m_plug);
        XSync(display, False);
        return;
    }

    const cairo_rectangle_int_t whole{0, 0, static_cast<int>(std::ceil(size.x)), static_cast<int>(std::ceil(size.y))};
    cairo_region_t* region = cairo_region_create_rectangle(&whole);

    for (const ImRect& hole : holes) {
        const int left = static_cast<int>(std::floor(hole.Min.x));
        const int top = static_cast<int>(std::floor(hole.Min.y));
        const cairo_rectangle_int_t removed{left, top, static_cast<int>(std::ceil(hole.Max.x)) - left, static_cast<int>(std::ceil(hole.Max.y)) - top};
        cairo_region_subtract_rectangle(region, &removed);
    }

    std::vector<XRectangle> kept(static_cast<std::size_t>(cairo_region_num_rectangles(region)));

    for (std::size_t index = 0; index < kept.size(); ++index) {
        cairo_rectangle_int_t rectangle;
        cairo_region_get_rectangle(region, static_cast<int>(index), &rectangle);
        kept[index] = XRectangle{static_cast<short>(rectangle.x), static_cast<short>(rectangle.y), static_cast<unsigned short>(rectangle.width), static_cast<unsigned short>(rectangle.height)};
    }

    cairo_region_destroy(region);

    // The server applies the shape before the product draws again, because the drawing of the product travels on another connection.
    XShapeCombineRectangles(display, plug, ShapeBounding, 0, 0, kept.data(), static_cast<int>(kept.size()), ShapeSet, YXBanded);
    XShapeCombineRectangles(display, plug, ShapeInput, 0, 0, kept.data(), static_cast<int>(kept.size()), ShapeSet, YXBanded);
    XSync(display, False);
}

// A press inside the page takes the keyboard for the plug, because no embedding socket exists on the other side to hand it over.
gboolean LinuxWebView::pressed(GtkWidget*, GdkEvent*, gpointer view) {
    static_cast<LinuxWebView*>(view)->focus();

    return FALSE;
}

void LinuxWebView::notified(GObject*, GParamSpec*, gpointer view) {
    static_cast<LinuxWebView*>(view)->refresh();
}

void LinuxWebView::travelled(WebKitBackForwardList*, WebKitBackForwardListItem*, gpointer, gpointer view) {
    static_cast<LinuxWebView*>(view)->refresh();
}

void LinuxWebView::posted(WebKitUserContentManager*, WebKitJavascriptResult* result, gpointer view) {
    gchar* json = jsc_value_to_json(webkit_javascript_result_get_js_value(result), 0);

    if (json == nullptr) {
        return;
    }

    static_cast<LinuxWebView*>(view)->receive(json);
    g_free(json);
}

GtkWidget* LinuxWebView::created(WebKitWebView*, WebKitNavigationAction*, gpointer view) {
    auto* opener = static_cast<LinuxWebView*>(view);
    return opener->opensPopups() ? opener->openPopup() : nullptr;
}

void LinuxWebView::closed(WebKitWebView*, gpointer view) {
    static_cast<LinuxWebView*>(view)->closeRequested();
}

// The engine downloads a response sent as an attachment by itself and ignores one it cannot show, which is downloaded here instead.
gboolean LinuxWebView::decided(WebKitWebView*, WebKitPolicyDecision* decision, WebKitPolicyDecisionType type, gpointer) {
    if (type != WEBKIT_POLICY_DECISION_TYPE_RESPONSE || webkit_response_policy_decision_is_mime_type_supported(WEBKIT_RESPONSE_POLICY_DECISION(decision)) == TRUE) {
        return FALSE;
    }

    webkit_policy_decision_download(decision);

    return TRUE;
}

// Only the view whose page started a download follows it, since every related view hears the context announce it.
void LinuxWebView::started(WebKitWebContext*, WebKitDownload* download, gpointer view) {
    auto* self = static_cast<LinuxWebView*>(view);

    if (webkit_download_get_web_view(download) != self->m_browser) {
        return;
    }

    self->m_active.push_back({WEBKIT_DOWNLOAD(g_object_ref(download)), {}, false});
    g_signal_connect(G_OBJECT(download), "decide-destination", G_CALLBACK(&LinuxWebView::destined), view);
    g_signal_connect(G_OBJECT(download), "failed", G_CALLBACK(&LinuxWebView::interrupted), view);
    g_signal_connect(G_OBJECT(download), "finished", G_CALLBACK(&LinuxWebView::completed), view);
}

// The engine takes the destination of a download as a file address.
gboolean LinuxWebView::destined(WebKitDownload* download, gchar* suggested, gpointer view) {
    auto* self = static_cast<LinuxWebView*>(view);
    Download* followed = self->active(download);

    if (followed == nullptr) {
        return FALSE;
    }

    followed->path = DownloadTarget::choose(self->m_downloads, text(suggested));
    gchar* address = g_filename_to_uri(followed->path.c_str(), nullptr, nullptr);
    webkit_download_set_destination(download, address);
    g_free(address);

    return TRUE;
}

void LinuxWebView::interrupted(WebKitDownload* download, GError* error, gpointer view) {
    auto* self = static_cast<LinuxWebView*>(view);
    Download* followed = self->active(download);

    if (followed == nullptr) {
        return;
    }

    followed->failed = true;
    self->downloaded({followed->path, false, error != nullptr ? text(error->message) : std::string()});
}

// A download ends once, after its failure when it failed, and the view lets go of it then.
void LinuxWebView::completed(WebKitDownload* download, gpointer view) {
    auto* self = static_cast<LinuxWebView*>(view);
    // clang-format off
    const auto found = std::ranges::find_if(self->m_active, [download](const Download& followed) { return followed.download == download; });
    // clang-format on

    if (found == self->m_active.end()) {
        return;
    }

    const Download ended = *found;
    self->m_active.erase(found);
    g_signal_handlers_disconnect_by_data(G_OBJECT(download), view);

    if (!ended.failed) {
        self->downloaded({ended.path, true, {}});
    }

    g_object_unref(download);
}

// A page asking for the camera or the microphone waits for the owner of the view, and every other question keeps the answer of the engine.
gboolean LinuxWebView::permitted(WebKitWebView*, WebKitPermissionRequest* request, gpointer view) {
    if (!WEBKIT_IS_USER_MEDIA_PERMISSION_REQUEST(request)) {
        return FALSE;
    }

    auto* self = static_cast<LinuxWebView*>(view);
    WebKitUserMediaPermissionRequest* media = WEBKIT_USER_MEDIA_PERMISSION_REQUEST(request);
    g_object_ref(request);
    // clang-format off
    const auto answer = [request](bool allowed) {
        (allowed ? webkit_permission_request_allow : webkit_permission_request_deny)(request);
        g_object_unref(request);
    };
    // clang-format on

    self->ask(text(webkit_web_view_get_uri(self->m_browser)), webkit_user_media_permission_is_for_video_device(media) == TRUE, webkit_user_media_permission_is_for_audio_device(media) == TRUE, answer);

    return TRUE;
}

std::string LinuxWebView::text(const gchar* value) {
    return value == nullptr ? std::string() : std::string(value);
}

// A window a page opens shares the process and the settings of its opener and keeps a content manager of its own, and its plug joins the product window like any view.
GtkWidget* LinuxWebView::openPopup() {
    WebKitUserContentManager* messages = webkit_user_content_manager_new();
    GtkWidget* popup = GTK_WIDGET(g_object_new(WEBKIT_TYPE_WEB_VIEW, "related-view", m_browser, "settings", webkit_web_view_get_settings(m_browser), "user-content-manager", messages, nullptr));
    g_object_unref(messages);
    GtkWidget* plug = gtk_plug_new(m_parent);
    gtk_container_add(GTK_CONTAINER(plug), popup);
    offer(std::make_unique<LinuxWebView>(tracker(), plug, WEBKIT_WEB_VIEW(popup), nullptr, m_parent, m_downloads));

    return popup;
}

LinuxWebView::Download* LinuxWebView::active(WebKitDownload* download) {
    // clang-format off
    const auto found = std::ranges::find_if(m_active, [download](const Download& followed) { return followed.download == download; });
    // clang-format on
    return found != m_active.end() ? &*found : nullptr;
}

void LinuxWebView::refresh() {
    report({text(webkit_web_view_get_uri(m_browser)), text(webkit_web_view_get_title(m_browser)), {}, webkit_web_view_is_loading(m_browser) == TRUE, webkit_web_view_can_go_back(m_browser) == TRUE, webkit_web_view_can_go_forward(m_browser) == TRUE});
}

void LinuxWebView::focus() {
    GdkWindow* window = gtk_widget_get_window(m_plug);

    if (window == nullptr) {
        return;
    }

    Display* display = GDK_WINDOW_XDISPLAY(window);
    const Window plug = GDK_WINDOW_XID(window);
    XSetInputFocus(display, plug, RevertToParent, CurrentTime);

    for (const long opcode : {xembedWindowActivate, xembedFocusIn}) {
        XEvent event{};
        event.xclient.type = ClientMessage;
        event.xclient.window = plug;
        event.xclient.message_type = XInternAtom(display, "_XEMBED", False);
        event.xclient.format = 32;
        event.xclient.data.l[0] = CurrentTime;
        event.xclient.data.l[1] = opcode;
        event.xclient.data.l[2] = xembedFocusCurrent;
        XSendEvent(display, plug, False, NoEventMask, &event);
    }

    XFlush(display);
}

} // namespace workpane::platform
