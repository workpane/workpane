#include "platform/windows/WindowsWebView.h"

#include "platform/DownloadTarget.h"
#include "platform/windows/WindowsTextHelper.h"
#include "platform/windows/WindowsWebEnvironment.h"

#include <wrl.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

namespace workpane::platform {

WindowsWebView::WindowsWebView(WebViewTracker& tracker, HWND parent, std::filesystem::path downloads, bool debug) : TrackedWebView(tracker), m_widget(createHost(parent)), m_parent(parent), m_downloads(std::move(downloads)), m_debug(debug) {}

// A window a page asked for and never showed is refused, which the page sees as a blocked window.
WindowsWebView::~WindowsWebView() {
    finishOpening(false);

    if (m_browser != nullptr) {
        m_browser->remove_SourceChanged(m_sourceToken);
        m_browser->remove_DocumentTitleChanged(m_titleToken);
        m_browser->remove_HistoryChanged(m_historyToken);
        m_browser->remove_NavigationStarting(m_startingToken);
        m_browser->remove_NavigationCompleted(m_completedToken);
        m_browser->remove_WebMessageReceived(m_messageToken);
        m_browser->remove_NewWindowRequested(m_windowToken);
        m_browser->remove_WindowCloseRequested(m_closeToken);

        if (Microsoft::WRL::ComPtr<ICoreWebView2_4> downloader; SUCCEEDED(m_browser.As(&downloader))) {
            downloader->remove_DownloadStarting(m_downloadToken);
        }

        m_browser.Reset();
    }

    if (m_controller != nullptr) {
        m_controller->Close();
        m_controller.Reset();
    }

    if (m_widget != nullptr) {
        DestroyWindow(m_widget);
    }
}

// The page is built once the environment of the product and then its own controller arrive, and a failure on the way is reported once in place of the page.
void WindowsWebView::build(WindowsWebEnvironment& environment) {
    const std::weak_ptr<bool> alive = m_alive;
    // clang-format off
    const auto ready = [this, alive](const WindowsWebEnvironment::Started& started) {
        if (alive.expired()) {
            return;
        }

        if (!started.hasValue()) {
            failed(started.error());
            return;
        }

        create(started.value().Get());
    };
    // clang-format on

    environment.request(ready);
}

Result<void> WindowsWebView::navigate(std::string_view url) {
    if (!m_open) {
        m_content = Content{WindowsTextHelper::wide(url), false};
        return Result<void>::success();
    }

    if (FAILED(m_browser->Navigate(WindowsTextHelper::wide(url).c_str()))) {
        return Result<void>::failure({"webview_navigate_failed", "The web view refused the address", std::string(url)});
    }

    return Result<void>::success();
}

Result<void> WindowsWebView::setHtml(std::string_view html) {
    if (!m_open) {
        m_content = Content{WindowsTextHelper::wide(html), true};
        return Result<void>::success();
    }

    if (FAILED(m_browser->NavigateToString(WindowsTextHelper::wide(html).c_str()))) {
        return Result<void>::failure({"webview_html_failed", "The web view refused the document", {}});
    }

    return Result<void>::success();
}

// A script asked for before the page opened, or before the first document it opens with loaded, runs in that document once it is in place, as it would on a view built at once.
Result<void> WindowsWebView::evaluate(std::string_view script) {
    if (m_refused) {
        return Result<void>::failure({"webview_evaluate_failed", "The web view could not be built, so it runs no script", {}});
    }

    if (!m_open || m_awaitingDocument) {
        m_scripts.push_back(WindowsTextHelper::wide(script));
        return Result<void>::success();
    }

    return execute(WindowsTextHelper::wide(script));
}

Result<void> WindowsWebView::execute(const std::wstring& script) {
    // clang-format off
    const auto finished = [](HRESULT, LPCWSTR) -> HRESULT { return S_OK; };
    // clang-format on

    if (FAILED(m_browser->ExecuteScript(script.c_str(), Microsoft::WRL::Callback<ICoreWebView2ExecuteScriptCompletedHandler>(finished).Get()))) {
        return Result<void>::failure({"webview_evaluate_failed", "The web view refused the script", {}});
    }

    return Result<void>::success();
}

void WindowsWebView::runScripts() {
    std::vector<std::wstring> scripts = std::move(m_scripts);
    m_scripts.clear();

    for (const std::wstring& script : scripts) {
        std::ignore = execute(script);
    }
}

// The controller is built for the child window of the view, and a controller arriving after its view went is closed at once.
void WindowsWebView::create(ICoreWebView2Environment* environment) {
    const std::weak_ptr<bool> alive = m_alive;
    // clang-format off
    const auto created = [this, alive](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
        if (alive.expired() && controller != nullptr) {
            controller->Close();
        }

        if (alive.expired()) {
            return S_OK;
        }

        if (FAILED(result) || controller == nullptr) {
            refuse(FAILED(result) ? result : E_POINTER);
            return S_OK;
        }

        attach(controller);

        return S_OK;
    };
    // clang-format on

    const HRESULT result = m_widget == nullptr ? E_HANDLE : environment->CreateCoreWebView2Controller(m_widget, Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(created).Get());

    if (FAILED(result)) {
        refuse(result);
    }
}

void WindowsWebView::attach(ICoreWebView2Controller* controller) {
    m_controller = controller;

    if (const HRESULT result = m_controller->get_CoreWebView2(&m_browser); FAILED(result)) {
        refuse(result);
        return;
    }

    // The developer tools open only in a debug run, and the engine draws no status bar over the page.
    if (Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings; SUCCEEDED(m_browser->get_Settings(&settings))) {
        settings->put_AreDevToolsEnabled(m_debug ? TRUE : FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
    }

    m_controller->put_Bounds(m_area);

    // The controller stays visible and the hidden window of the view keeps it off screen, since WebView2 holds back the end of a download in a controller that is not visible.
    m_controller->put_IsVisible(TRUE);

    // The page announces every change of its address, title and history, and a navigation starts and completes the loading it reports.
    // clang-format off
    const auto moved = [this](ICoreWebView2*, ICoreWebView2SourceChangedEventArgs*) -> HRESULT {
        refresh();

        return S_OK;
    };

    const auto changed = [this](ICoreWebView2*, IUnknown*) -> HRESULT {
        refresh();

        return S_OK;
    };

    const auto starting = [this](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*) -> HRESULT {
        m_loading = true;
        refresh();

        return S_OK;
    };

    const auto completed = [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
        m_loading = false;

        if (m_awaitingDocument) {
            m_awaitingDocument = false;
            runScripts();
        }

        refresh();

        return S_OK;
    };

    const auto posted = [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
        LPWSTR json = nullptr;

        if (SUCCEEDED(args->get_WebMessageAsJson(&json))) {
            receive(text(json));
        }

        return S_OK;
    };

    const auto requested = [this](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
        openPopup(args);

        return S_OK;
    };

    const auto closing = [this](ICoreWebView2*, IUnknown*) -> HRESULT {
        closeRequested();

        return S_OK;
    };

    const auto downloading = [this](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* args) -> HRESULT {
        Microsoft::WRL::ComPtr<ICoreWebView2DownloadOperation> operation;
        LPWSTR suggested = nullptr;

        if (FAILED(args->get_DownloadOperation(&operation)) || FAILED(args->get_ResultFilePath(&suggested))) {
            return S_OK;
        }

        const std::filesystem::path chosen = DownloadTarget::choose(m_downloads, text(suggested));
        args->put_ResultFilePath(chosen.c_str());
        args->put_Handled(TRUE);
        follow(operation.Get(), chosen);

        return S_OK;
    };

    // The page and a window a page opened show their first document only once the page script is in place, so that document already runs it.
    const auto added = [this, alive = std::weak_ptr<bool>(m_alive)](HRESULT result, LPCWSTR) -> HRESULT {
        if (alive.expired()) {
            return S_OK;
        }

        if (FAILED(result)) {
            refuse(result);
            return S_OK;
        }

        open();

        return S_OK;
    };
    // clang-format on

    m_browser->add_SourceChanged(Microsoft::WRL::Callback<ICoreWebView2SourceChangedEventHandler>(moved).Get(), &m_sourceToken);
    m_browser->add_DocumentTitleChanged(Microsoft::WRL::Callback<ICoreWebView2DocumentTitleChangedEventHandler>(changed).Get(), &m_titleToken);
    m_browser->add_HistoryChanged(Microsoft::WRL::Callback<ICoreWebView2HistoryChangedEventHandler>(changed).Get(), &m_historyToken);
    m_browser->add_NavigationStarting(Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(starting).Get(), &m_startingToken);
    m_browser->add_NavigationCompleted(Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(completed).Get(), &m_completedToken);
    m_browser->add_WebMessageReceived(Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(posted).Get(), &m_messageToken);
    m_browser->add_NewWindowRequested(Microsoft::WRL::Callback<ICoreWebView2NewWindowRequestedEventHandler>(requested).Get(), &m_windowToken);
    m_browser->add_WindowCloseRequested(Microsoft::WRL::Callback<ICoreWebView2WindowCloseRequestedEventHandler>(closing).Get(), &m_closeToken);

    // Every download is saved in the downloads folder the product was given and reported to the owner of the view, instead of the flyout of WebView2.
    if (Microsoft::WRL::ComPtr<ICoreWebView2_4> downloader; SUCCEEDED(m_browser.As(&downloader))) {
        downloader->add_DownloadStarting(Microsoft::WRL::Callback<ICoreWebView2DownloadStartingEventHandler>(downloading).Get(), &m_downloadToken);
    }

    if (const HRESULT result = m_browser->AddScriptToExecuteOnDocumentCreated(WindowsTextHelper::wide(pageScript).c_str(), Microsoft::WRL::Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(added).Get()); FAILED(result)) {
        refuse(result);
    }
}

// The page opens what was asked of it before it existed, and a window a page opened takes its place.
void WindowsWebView::open() {
    // A window a page opened and a page given a document before it opened wait for that first document before the scripts asked for run.
    const bool adopted = m_opening.has_value();
    m_open = true;
    m_awaitingDocument = adopted || m_content.has_value();
    finishOpening(true);

    if (!m_content.has_value()) {
        if (!m_awaitingDocument) {
            runScripts();
        }

        return;
    }

    const Content content = std::move(*m_content);
    m_content.reset();

    if (content.html) {
        m_browser->NavigateToString(content.text.c_str());
        return;
    }

    m_browser->Navigate(content.text.c_str());
}

// A page that could not be built reports once in place of the page, and a page that asked for it as a window is refused.
void WindowsWebView::refuse(HRESULT result) {
    m_refused = true;
    m_scripts.clear();
    finishOpening(false);
    failed(WindowsWebEnvironment::failure(result));
}

void WindowsWebView::reload() {
    if (m_browser != nullptr) {
        m_browser->Reload();
    }
}

void WindowsWebView::back() {
    if (m_browser != nullptr) {
        m_browser->GoBack();
    }
}

void WindowsWebView::forward() {
    if (m_browser != nullptr) {
        m_browser->GoForward();
    }
}

void WindowsWebView::stop() {
    if (m_browser != nullptr) {
        m_browser->Stop();
    }
}

// The controller follows the child window, and a controller that arrives later takes the last place the view was given.
void WindowsWebView::apply(const ImRect& bounds, bool visible) {
    const int left = static_cast<int>(std::lround(bounds.Min.x));
    const int top = static_cast<int>(std::lround(bounds.Min.y));
    const int width = static_cast<int>(std::lround(bounds.Max.x)) - left;
    const int height = static_cast<int>(std::lround(bounds.Max.y)) - top;
    MoveWindow(m_widget, left, top, width, height, TRUE);
    ShowWindow(m_widget, visible ? SW_SHOW : SW_HIDE);
    m_area = RECT{0, 0, width, height};

    if (m_controller != nullptr) {
        m_controller->put_Bounds(m_area);
    }
}

// The child window takes a region without the holes, which the system clips its drawing and its pointer to, and the region then belongs to the window.
void WindowsWebView::cut(ImVec2 size, const std::vector<ImRect>& holes) {
    if (holes.empty()) {
        SetWindowRgn(m_widget, nullptr, TRUE);
        return;
    }

    HRGN region = CreateRectRgn(0, 0, static_cast<int>(std::ceil(size.x)), static_cast<int>(std::ceil(size.y)));

    for (const ImRect& hole : holes) {
        HRGN removed = CreateRectRgn(static_cast<int>(std::floor(hole.Min.x)), static_cast<int>(std::floor(hole.Min.y)), static_cast<int>(std::ceil(hole.Max.x)), static_cast<int>(std::ceil(hole.Max.y)));
        CombineRgn(region, region, removed, RGN_DIFF);
        DeleteObject(removed);
    }

    SetWindowRgn(m_widget, region, TRUE);
}

// The browser hands out text it allocated, which is read once and released here.
std::string WindowsWebView::text(LPWSTR value) {
    if (value == nullptr) {
        return {};
    }

    std::string narrowed = WindowsTextHelper::narrow(value);
    CoTaskMemFree(value);

    return narrowed;
}

// The child window of a view only hosts its controller, so its class answers every message the default way.
HWND WindowsWebView::createHost(HWND parent) {
    HINSTANCE module = GetModuleHandleW(nullptr);
    WNDCLASSEXW known{};
    known.cbSize = static_cast<UINT>(sizeof(known));

    if (GetClassInfoExW(module, hostClass, &known) == FALSE) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = static_cast<UINT>(sizeof(windowClass));
        windowClass.lpfnWndProc = DefWindowProcW;
        windowClass.hInstance = module;
        windowClass.lpszClassName = hostClass;
        RegisterClassExW(&windowClass);
    }

    return CreateWindowExW(0, hostClass, L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, parent, nullptr, module, nullptr);
}

// A window a page opens is built in the environment of its opener while the request waits, and a view that cannot take windows refuses it, which the page sees as a blocked window.
void WindowsWebView::openPopup(ICoreWebView2NewWindowRequestedEventArgs* request) {
    Microsoft::WRL::ComPtr<ICoreWebView2_2> browser;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment;
    Microsoft::WRL::ComPtr<ICoreWebView2Deferral> deferral;

    if (!opensPopups() || FAILED(m_browser.As(&browser)) || FAILED(browser->get_Environment(&environment)) || FAILED(request->GetDeferral(&deferral))) {
        request->put_Handled(TRUE);
        return;
    }

    auto popup = std::make_unique<WindowsWebView>(tracker(), m_parent, m_downloads, m_debug);
    popup->m_opening = Opening{Microsoft::WRL::ComPtr<ICoreWebView2NewWindowRequestedEventArgs>(request), deferral};
    popup->create(environment.Get());
    offer(std::move(popup));
}

// The page that opened the window learns it opened once the window can run its first document, and a window gone before that is refused, which the page sees as a blocked window.
void WindowsWebView::finishOpening(bool shown) {
    if (!m_opening.has_value()) {
        return;
    }

    const Opening opening = std::move(*m_opening);
    m_opening.reset();

    if (shown) {
        opening.request->put_NewWindow(m_browser.Get());
    } else {
        opening.request->put_Handled(TRUE);
    }

    opening.deferral->Complete();
}

// A download is held until it completed or was interrupted, since WebView2 tells nothing about an operation nobody holds, and it reports once then, and nothing once its view is gone.
void WindowsWebView::follow(ICoreWebView2DownloadOperation* operation, std::filesystem::path path) {
    const std::weak_ptr<bool> alive = m_alive;
    // clang-format off
    const auto changed = [this, alive, path](ICoreWebView2DownloadOperation* sender, IUnknown*) -> HRESULT {
        if (!alive.expired()) {
            settle(sender, path);
        }

        return S_OK;
    };
    // clang-format on

    m_downloading.emplace_back(operation);
    EventRegistrationToken token{};
    operation->add_StateChanged(Microsoft::WRL::Callback<ICoreWebView2StateChangedEventHandler>(changed).Get(), &token);

    // A download that ended before its handler was in place is reported now.
    settle(operation, path);
}

void WindowsWebView::settle(ICoreWebView2DownloadOperation* operation, const std::filesystem::path& path) {
    // clang-format off
    const auto held = std::ranges::find_if(m_downloading, [operation](const Microsoft::WRL::ComPtr<ICoreWebView2DownloadOperation>& kept) { return kept.Get() == operation; });
    // clang-format on
    COREWEBVIEW2_DOWNLOAD_STATE state = COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS;

    if (held == m_downloading.end() || FAILED(operation->get_State(&state)) || state == COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS) {
        return;
    }

    COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON reason = COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_NONE;
    std::ignore = operation->get_InterruptReason(&reason);
    const bool finished = state == COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED;
    m_downloading.erase(held);

    downloaded({path, finished, finished ? std::string() : "WebView2 interrupted the download with reason " + std::to_string(static_cast<int>(reason))});
}

void WindowsWebView::refresh() {
    LPWSTR source = nullptr;
    LPWSTR title = nullptr;
    BOOL back = FALSE;
    BOOL forward = FALSE;
    m_browser->get_Source(&source);
    m_browser->get_DocumentTitle(&title);
    m_browser->get_CanGoBack(&back);
    m_browser->get_CanGoForward(&forward);
    report({text(source), text(title), {}, m_loading, back == TRUE, forward == TRUE});
}

} // namespace workpane::platform
