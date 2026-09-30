#pragma once

#include "Result.h"
#include "platform/TrackedWebView.h"

#include <WebView2.h>
#include <imgui_internal.h>
#include <windows.h>
#include <wrl/client.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::platform {

class WebViewTracker;
class WindowsWebEnvironment;

// A page of WebView2 hosted by a child window of its own inside the product window, which is moved to the rectangle of its component.
// Its controller is built in the background, in the environment of the product or in the one of the page that opened it, and an address asked for before the page exists opens once it does.
class WindowsWebView final : public TrackedWebView {
  public:
    WindowsWebView(WebViewTracker& tracker, HWND parent, std::filesystem::path downloads, bool debug);
    ~WindowsWebView() override;

    WindowsWebView(const WindowsWebView&) = delete;
    WindowsWebView& operator=(const WindowsWebView&) = delete;

    void build(WindowsWebEnvironment& environment);

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
    static constexpr const wchar_t* hostClass{L"WorkpaneWebViewHost"};

    struct Opening final {
        Microsoft::WRL::ComPtr<ICoreWebView2NewWindowRequestedEventArgs> request;
        Microsoft::WRL::ComPtr<ICoreWebView2Deferral> deferral;
    };

    struct Content final {
        std::wstring text;
        bool html;
    };

    [[nodiscard]] static std::string text(LPWSTR value);
    [[nodiscard]] static HWND createHost(HWND parent);

    void create(ICoreWebView2Environment* environment);
    void attach(ICoreWebView2Controller* controller);
    void open();
    void refuse(HRESULT result);
    void runScripts();
    [[nodiscard]] Result<void> execute(const std::wstring& script);
    void openPopup(ICoreWebView2NewWindowRequestedEventArgs* request);
    void finishOpening(bool shown);
    void follow(ICoreWebView2DownloadOperation* operation, std::filesystem::path path);
    void settle(ICoreWebView2DownloadOperation* operation, const std::filesystem::path& path);
    void refresh();

    HWND m_widget;
    HWND m_parent;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> m_controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> m_browser;
    EventRegistrationToken m_sourceToken{};
    EventRegistrationToken m_titleToken{};
    EventRegistrationToken m_startingToken{};
    EventRegistrationToken m_completedToken{};
    EventRegistrationToken m_historyToken{};
    EventRegistrationToken m_messageToken{};
    EventRegistrationToken m_windowToken{};
    EventRegistrationToken m_closeToken{};
    EventRegistrationToken m_downloadToken{};
    std::filesystem::path m_downloads;
    std::optional<Opening> m_opening;
    std::optional<Content> m_content;
    std::vector<std::wstring> m_scripts;
    std::vector<Microsoft::WRL::ComPtr<ICoreWebView2DownloadOperation>> m_downloading;
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)};
    RECT m_area{};
    bool m_debug;
    bool m_shown{false};
    bool m_open{false};
    bool m_awaitingDocument{false};
    bool m_refused{false};
    bool m_loading{false};
};

} // namespace workpane::platform
