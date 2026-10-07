#pragma once

#include "Result.h"
#include "support/WebViewRecord.h"
#include "ui/NativeViewHost.h"
#include "ui/NativeWebView.h"

#include <memory>
#include <vector>

namespace workpane::tests {

class FakeNativeViewHost final : public ui::NativeViewHost {
  public:
    explicit FakeNativeViewHost(std::shared_ptr<WebViewRecord> record);

    [[nodiscard]] Result<std::unique_ptr<ui::NativeWebView>> createWebView() override;
    [[nodiscard]] bool pump() override;
    void watch() override;
    void endFrame(const std::vector<ImRect>& floating) override;

  private:
    std::shared_ptr<WebViewRecord> m_record;
};

} // namespace workpane::tests
