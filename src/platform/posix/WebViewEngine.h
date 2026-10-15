#pragma once

#include "Result.h"

#include <filesystem>
#include <memory>

namespace workpane::platform {

// One `webview/webview` instance hosted by a native window this product owns, which builds the native page the platform view then drives.
// Its cookies, storage and cache live under the data directory it is given, as far as the platform lets them.
class WebViewEngine final {
  public:
    [[nodiscard]] static Result<std::unique_ptr<WebViewEngine>> create(void* hostWindow, bool debug, const std::filesystem::path& dataDirectory);

    ~WebViewEngine();
    WebViewEngine(const WebViewEngine&) = delete;
    WebViewEngine& operator=(const WebViewEngine&) = delete;

    [[nodiscard]] void* widget() const;
    [[nodiscard]] void* controller() const;

  private:
    explicit WebViewEngine(void* webview);

    void* m_webview{nullptr};
};

} // namespace workpane::platform
