#include "ui/components/views/WebView.h"

#include "platform/PathTextHelper.h"
#include "ui/NativeViewHost.h"
#include "ui/TextCaseHelper.h"
#include "ui/WebDownload.h"
#include "ui/WebNavigation.h"
#include "ui/WebPermission.h"
#include "ui/WidgetHelper.h"
#include "ui/theme/Theme.h"

#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

namespace workpane::ui {

WebView::WebView(NodeId id) : Component(id) {}

std::string_view WebView::kind() const {
    return "webView";
}

Result<void> WebView::command(RenderContext& context, std::string_view name, const json::Json& arguments) {
    if (m_view == nullptr) {
        return Result<void>::failure({"webview_unavailable", "The web view has no native page yet", std::string(name)});
    }

    if (name == "navigate") {
        std::string url;
        json::ObjectReader reader(arguments, "webView.navigate");
        reader.readText("url", url);

        if (const auto finished = reader.finish(); !finished.hasValue()) {
            return finished;
        }

        if (!webAddress(url)) {
            return Result<void>::failure({"webview_address_invalid", "A web view opens only web, file and about addresses", url});
        }

        return m_view->navigate(url);
    }

    // The history and the loading are driven by the engine itself, which keeps working on a page that failed to load.
    if (name == "reload" && arguments.empty()) {
        m_view->reload();
        return Result<void>::success();
    }

    if (name == "back" && arguments.empty()) {
        m_view->back();
        return Result<void>::success();
    }

    if (name == "forward" && arguments.empty()) {
        m_view->forward();
        return Result<void>::success();
    }

    if (name == "stop" && arguments.empty()) {
        m_view->stop();
        return Result<void>::success();
    }

    // The owner answers a page that asked for the camera or the microphone, and an answer to a request already answered changes nothing.
    if (name == "answer-permission") {
        std::int64_t request = 0;
        bool allowed = false;
        json::ObjectReader reader(arguments, "webView.answer-permission");
        reader.readInteger("request", request, 1, largestPopup).read("allowed", allowed);

        if (const auto finished = reader.finish(); !finished.hasValue()) {
            return finished;
        }

        m_view->answerPermission(static_cast<std::uint64_t>(request), allowed);
        return Result<void>::success();
    }

    return Component::command(context, name, arguments);
}

void WebView::detach(RenderContext&) {
    m_view.reset();
}

// A window a page opened is adopted as soon as its web view is mounted, drawn or not, so it is never closed for waiting.
// Pages report from the event loop of the platform, and a page behind another tab reports as soon as one that is drawn.
void WebView::update(RenderContext& context) {
    if (m_buildFailure->has_value()) {
        const Error failure = std::move(**m_buildFailure);
        m_buildFailure->reset();
        m_view.reset();
        showFailure(context, failure);
        context.requestFrame();
    }

    if (m_popup != 0 && m_view == nullptr) {
        std::ignore = ensureView(context);
    }

    while (!m_pending->empty()) {
        auto [name, value] = std::move(m_pending->front());
        m_pending->pop_front();
        context.emit(id(), name, std::move(value));
    }
}

Alignment WebView::defaultRowAlignment() const {
    return Alignment::Stretch;
}

void WebView::readProperties(json::ObjectReader& reader) {
    std::string url = m_url;
    std::string html = m_html;
    reader.read("url", url, json::Presence::Optional).read("html", html, json::Presence::Optional).readInteger("popup", m_popup, 1, largestPopup, json::Presence::Optional);

    if (!url.empty() && !webAddress(url)) {
        fail({"webview_address_invalid", "A web view opens only web, file and about addresses", "webView.url"});
        return;
    }

    if (url != m_url || html != m_html) {
        m_url = std::move(url);
        m_html = std::move(html);
        m_contentChanged = true;
    }
}

Component::Restore WebView::keep() {
    return kept(m_url, m_html, m_popup, m_contentChanged);
}

Result<void> WebView::validate() const {
    if ((!m_url.empty() && !m_html.empty()) || (m_popup != 0 && (!m_url.empty() || !m_html.empty()))) {
        return Result<void>::failure({"webview_content_ambiguous", "A web view shows an address, a document or a window a page opened, never two of them", m_url});
    }

    return Result<void>::success();
}

ImVec2 WebView::measureContent(RenderContext& context, float availableWidth) {
    return {availableWidth, webViewMinimumHeight * context.scale()};
}

void WebView::render(RenderContext& context, const ImRect& bounds) {
    std::ignore = WidgetHelper::interact(ImGui::GetID("##webView"), bounds);

    if (!ensureView(context)) {
        WidgetHelper::paragraph(context, *ImGui::GetWindowDrawList(), context.font(ThemeFont::Interface), bounds, context.color(ThemeColor::DangerText), context.translate(failureKey(m_failure)), TextAlign::Center);
        return;
    }

    if (m_contentChanged) {
        load(context);
    }

    const ImRect clipped(ImMax(bounds.Min, ImGui::GetWindowDrawList()->GetClipRectMin()), ImMin(bounds.Max, ImGui::GetWindowDrawList()->GetClipRectMax()));
    m_view->place(clipped, !context.modalActive() && clipped.GetWidth() > 0.0F && clipped.GetHeight() > 0.0F);
}

bool WebView::ensureView(RenderContext& context) {
    if (m_view != nullptr) {
        return true;
    }

    if (!m_failure.empty()) {
        return false;
    }

    auto created = m_popup != 0 ? adopt(context) : context.nativeViews().createWebView();

    if (!created.hasValue()) {
        showFailure(context, created.error());
        return false;
    }

    m_view = std::move(created.value());
    const std::weak_ptr<std::deque<std::pair<std::string, json::Json>>> pending = m_pending;
    // clang-format off
    const auto navigated = [pending](WebNavigation navigation) {
        if (const auto queue = pending.lock(); queue != nullptr) {
            queue->emplace_back("navigation", json::Json{{"url", navigation.url}, {"title", navigation.title}, {"icon", navigation.icon}, {"loading", navigation.loading}, {"canGoBack", navigation.canGoBack}, {"canGoForward", navigation.canGoForward}});
        }
    };

    const auto opened = [pending](std::string url, bool background) {
        if (const auto queue = pending.lock(); queue != nullptr) {
            queue->emplace_back("open-request", json::Json{{"url", std::move(url)}, {"background", background}});
        }
    };

    const auto popped = [pending, context = &context](std::unique_ptr<NativeWebView> popup) {
        if (const auto queue = pending.lock(); queue != nullptr) {
            queue->emplace_back("popup", json::Json{{"popup", context->keepPopup(std::move(popup))}});
        }
    };

    const auto closed = [pending]() {
        if (const auto queue = pending.lock(); queue != nullptr) {
            queue->emplace_back("close-request", json::Json::object());
        }
    };

    const auto asked = [pending](WebPermission permission) {
        if (const auto queue = pending.lock(); queue != nullptr) {
            queue->emplace_back("permission-request", json::Json{{"request", permission.request}, {"origin", std::move(permission.origin)}, {"camera", permission.camera}, {"microphone", permission.microphone}});
        }
    };

    const auto downloaded = [pending](WebDownload download) {
        if (const auto queue = pending.lock(); queue != nullptr) {
            queue->emplace_back("download", json::Json{{"path", platform::PathTextHelper::generic(download.path)}, {"finished", download.finished}, {"message", std::move(download.message)}});
        }
    };

    const auto broke = [failure = std::weak_ptr<std::optional<Error>>(m_buildFailure)](const Error& error) {
        if (const auto kept = failure.lock(); kept != nullptr) {
            *kept = error;
        }
    };
    // clang-format on

    m_view->setNavigationHandler(navigated);
    m_view->setOpenHandler(opened);
    m_view->setPopupHandler(popped);
    m_view->setCloseHandler(closed);
    m_view->setDownloadHandler(downloaded);
    m_view->setPermissionHandler(asked);
    m_view->setFailureHandler(broke);

    return true;
}

// An address opens when it names the web, a file or an about page, which are the only schemes a page of the product needs.
bool WebView::webAddress(std::string_view url) {
    const std::size_t colon = url.find(':');

    if (colon == std::string_view::npos || colon == 0) {
        return false;
    }

    const std::string scheme = TextCaseHelper::lower(url.substr(0, colon));
    return scheme == "http" || scheme == "https" || scheme == "file" || scheme == "about";
}

std::string_view WebView::failureKey(std::string_view code) {
    return code == "webview_popup_missing" ? "workpane.webview.popup-missing" : "workpane.webview.unavailable";
}

Result<std::unique_ptr<NativeWebView>> WebView::adopt(RenderContext& context) const {
    auto popup = context.takePopup(static_cast<std::uint64_t>(m_popup));

    if (popup == nullptr) {
        return Result<std::unique_ptr<NativeWebView>>::failure({"webview_popup_missing", "The window the page opened is no longer waiting", std::to_string(m_popup)});
    }

    return Result<std::unique_ptr<NativeWebView>>::success(std::move(popup));
}

// The failure takes the place of the page, and its code reaches the owner once however often the view fails.
void WebView::showFailure(RenderContext& context, const Error& error) {
    m_failure = error.code;

    if (m_failureReported) {
        return;
    }

    m_failureReported = true;
    context.emit(id(), "error", {{"code", error.code}, {"message", error.message}});
}

void WebView::load(RenderContext& context) {
    m_contentChanged = false;
    const auto loaded = m_html.empty() ? (m_url.empty() ? Result<void>::success() : m_view->navigate(m_url)) : m_view->setHtml(m_html);

    if (!loaded.hasValue()) {
        context.emit(id(), "error", {{"code", loaded.error().code}, {"message", loaded.error().message}});
    }
}

} // namespace workpane::ui
