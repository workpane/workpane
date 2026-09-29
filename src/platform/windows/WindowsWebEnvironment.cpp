#include "platform/windows/WindowsWebEnvironment.h"

#include <wrl.h>

#include <cstdint>
#include <format>
#include <memory>
#include <utility>

namespace workpane::platform {

WindowsWebEnvironment::WindowsWebEnvironment(std::filesystem::path dataDirectory) : m_dataDirectory(std::move(dataDirectory)) {}

// The failure names the result WebView2 answered, such as the one of a system without its runtime.
Error WindowsWebEnvironment::failure(HRESULT result) {
    return {"webview_create_failed", "WebView2 could not build the page", std::format("HRESULT {:#010x}", static_cast<std::uint32_t>(result))};
}

// A view asking after the environment started or failed hears at once, and the first view to ask starts it.
void WindowsWebEnvironment::request(Ready ready) {
    if (m_started.has_value()) {
        ready(*m_started);
        return;
    }

    m_waiting.push_back(std::move(ready));

    if (m_starting) {
        return;
    }

    m_starting = true;
    const std::weak_ptr<bool> alive = m_alive;
    // clang-format off
    const auto created = [this, alive](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
        if (alive.expired()) {
            return S_OK;
        }

        settle(SUCCEEDED(result) && environment != nullptr ? Started::success(Microsoft::WRL::ComPtr<ICoreWebView2Environment>(environment)) : Started::failure(failure(FAILED(result) ? result : E_POINTER)));

        return S_OK;
    };
    // clang-format on

    const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(nullptr, m_dataDirectory.c_str(), nullptr, Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(created).Get());

    if (FAILED(result)) {
        settle(Started::failure(failure(result)));
    }
}

void WindowsWebEnvironment::settle(Started started) {
    m_started = std::move(started);
    const std::vector<Ready> waiting = std::exchange(m_waiting, {});

    for (const Ready& ready : waiting) {
        ready(*m_started);
    }
}

} // namespace workpane::platform
