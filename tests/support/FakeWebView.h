#pragma once

#include "Result.h"
#include "support/WebViewRecord.h"
#include "ui/NativeWebView.h"
#include "ui/WebNavigation.h"

#include <imgui_internal.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::tests {

// A web view that keeps a history of the addresses it was sent to and reports each of them loaded at once, the way a fast page would, unless the record holds it loading.
// Its handlers are kept in the record, so a test asks for a tab, opens a window or closes the page as a page of this view would.
class FakeWebView final : public ui::NativeWebView {
  public:
    explicit FakeWebView(std::shared_ptr<WebViewRecord> record);

    void place(const ImRect& bounds, bool visible) override;
    [[nodiscard]] Result<void> navigate(std::string_view url) override;
    [[nodiscard]] Result<void> setHtml(std::string_view html) override;
    [[nodiscard]] Result<void> evaluate(std::string_view script) override;
    void reload() override;
    void back() override;
    void forward() override;
    void stop() override;
    void setNavigationHandler(NavigationHandler handler) override;
    void setOpenHandler(OpenHandler handler) override;
    void setPopupHandler(PopupHandler handler) override;
    void setCloseHandler(CloseHandler handler) override;
    void setDownloadHandler(DownloadHandler handler) override;
    void setPermissionHandler(PermissionHandler handler) override;
    void setFailureHandler(FailureHandler handler) override;
    void answerPermission(std::uint64_t request, bool allowed) override;

  private:
    void report();

    std::shared_ptr<WebViewRecord> m_record;
    NavigationHandler m_navigation;
    std::vector<std::string> m_history;
    std::size_t m_position{0};
};

} // namespace workpane::tests
