#pragma once

#include "Error.h"
#include "Result.h"

#include <WebView2.h>
#include <windows.h>
#include <wrl/client.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace workpane::platform {

// The one environment of WebView2 the web views of the product window share, whose browser processes start in the background the first time a view asks for it.
// Its cookies, storage and cache live under the data directory it is given, and every view that asks hears once whether it started.
class WindowsWebEnvironment final {
  public:
    using Started = Result<Microsoft::WRL::ComPtr<ICoreWebView2Environment>>;
    using Ready = std::function<void(const Started& started)>;

    explicit WindowsWebEnvironment(std::filesystem::path dataDirectory);

    WindowsWebEnvironment(const WindowsWebEnvironment&) = delete;
    WindowsWebEnvironment& operator=(const WindowsWebEnvironment&) = delete;

    [[nodiscard]] static Error failure(HRESULT result);

    void request(Ready ready);

  private:
    void settle(Started started);

    std::filesystem::path m_dataDirectory;
    std::vector<Ready> m_waiting;
    std::optional<Started> m_started;
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)};
    bool m_starting{false};
};

} // namespace workpane::platform
