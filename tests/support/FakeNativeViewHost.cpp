#include "support/FakeNativeViewHost.h"

#include "support/FakeWebView.h"

#include <utility>

namespace workpane::tests {

FakeNativeViewHost::FakeNativeViewHost(std::shared_ptr<WebViewRecord> record) : m_record(std::move(record)) {}

Result<std::unique_ptr<ui::NativeWebView>> FakeNativeViewHost::createWebView() {
    ++m_record->created;

    return Result<std::unique_ptr<ui::NativeWebView>>::success(std::make_unique<FakeWebView>(m_record));
}

bool FakeNativeViewHost::pump() {
    return false;
}

void FakeNativeViewHost::watch() {}

void FakeNativeViewHost::endFrame(const std::vector<ImRect>& floating) {
    m_record->floating = floating;
}

} // namespace workpane::tests
