#pragma once

#include "Error.h"
#include "Result.h"
#include "ui/WebDownload.h"
#include "ui/WebNavigation.h"
#include "ui/WebPermission.h"

#include <imgui_internal.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace workpane::ui {

// One native web view placed over the window, which the component that owns it moves, shows and hides every frame.
// A page may ask for an address in a new tab, open a window of its own that stays related to it, ask for its own window to close, download files into the downloads folder of the reader and ask whether it may use the camera or the microphone.
// A platform that builds its views in the background reports once when a view could not be built.
class NativeWebView {
  public:
    using NavigationHandler = std::function<void(WebNavigation navigation)>;
    using OpenHandler = std::function<void(std::string url, bool background)>;
    using PopupHandler = std::function<void(std::unique_ptr<NativeWebView> popup)>;
    using CloseHandler = std::function<void()>;
    using DownloadHandler = std::function<void(WebDownload download)>;
    using PermissionHandler = std::function<void(WebPermission permission)>;
    using FailureHandler = std::function<void(const Error& error)>;

    virtual ~NativeWebView() = default;

    virtual void place(const ImRect& bounds, bool visible) = 0;
    [[nodiscard]] virtual Result<void> navigate(std::string_view url) = 0;
    [[nodiscard]] virtual Result<void> setHtml(std::string_view html) = 0;
    [[nodiscard]] virtual Result<void> evaluate(std::string_view script) = 0;
    virtual void reload() = 0;
    virtual void back() = 0;
    virtual void forward() = 0;
    virtual void stop() = 0;
    virtual void setNavigationHandler(NavigationHandler handler) = 0;
    virtual void setOpenHandler(OpenHandler handler) = 0;
    virtual void setPopupHandler(PopupHandler handler) = 0;
    virtual void setCloseHandler(CloseHandler handler) = 0;
    virtual void setDownloadHandler(DownloadHandler handler) = 0;
    virtual void setPermissionHandler(PermissionHandler handler) = 0;
    virtual void setFailureHandler(FailureHandler handler) = 0;
    virtual void answerPermission(std::uint64_t request, bool allowed) = 0;
};

} // namespace workpane::ui
