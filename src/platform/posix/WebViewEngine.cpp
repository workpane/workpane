#include "platform/posix/WebViewEngine.h"

#include <webview/webview.h>

#include <memory>

namespace workpane::platform {

Result<std::unique_ptr<WebViewEngine>> WebViewEngine::create(void* hostWindow, bool debug, const std::filesystem::path& dataDirectory) {
    webview_set_data_directory(dataDirectory.string().c_str());
    webview_t webview = webview_create(debug ? 1 : 0, hostWindow);

    if (webview == nullptr) {
        return Result<std::unique_ptr<WebViewEngine>>::failure({"webview_create_failed", "The platform web view could not be created", {}});
    }

    return Result<std::unique_ptr<WebViewEngine>>::success(std::unique_ptr<WebViewEngine>(new WebViewEngine(webview)));
}

WebViewEngine::WebViewEngine(void* webview) : m_webview(webview) {}

WebViewEngine::~WebViewEngine() {
    webview_destroy(static_cast<webview_t>(m_webview));
}

void* WebViewEngine::widget() const {
    return webview_get_native_handle(static_cast<webview_t>(m_webview), WEBVIEW_NATIVE_HANDLE_KIND_UI_WIDGET);
}

// The browser object of the platform, which the platform view drives directly.
void* WebViewEngine::controller() const {
    return webview_get_native_handle(static_cast<webview_t>(m_webview), WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER);
}

} // namespace workpane::platform
