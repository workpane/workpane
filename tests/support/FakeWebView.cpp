#include "support/FakeWebView.h"

#include <string>
#include <utility>

namespace workpane::tests {

FakeWebView::FakeWebView(std::shared_ptr<WebViewRecord> record) : m_record(std::move(record)) {}

void FakeWebView::place(const ImRect&, bool visible) {
    if (visible) {
        ++m_record->placedVisible;
    }
}

// A new address drops the pages ahead of the current one, as the history of a browser does.
Result<void> FakeWebView::navigate(std::string_view url) {
    m_record->navigations.emplace_back(url);

    if (!m_history.empty()) {
        m_history.resize(m_position + 1);
    }

    m_history.emplace_back(url);
    m_position = m_history.size() - 1;
    report();

    return Result<void>::success();
}

Result<void> FakeWebView::setHtml(std::string_view html) {
    m_record->documents.emplace_back(html);

    return Result<void>::success();
}

Result<void> FakeWebView::evaluate(std::string_view) {
    return Result<void>::success();
}

void FakeWebView::reload() {
    m_record->commands.emplace_back("reload");
}

void FakeWebView::back() {
    m_record->commands.emplace_back("back");

    if (m_position > 0) {
        --m_position;
        report();
    }
}

void FakeWebView::forward() {
    m_record->commands.emplace_back("forward");

    if (m_position + 1 < m_history.size()) {
        ++m_position;
        report();
    }
}

// A page held loading finishes when it is stopped, as a page the reader gave up on does.
void FakeWebView::stop() {
    m_record->commands.emplace_back("stop");
    m_record->holdLoading = false;
    report();
}

// A view adopted after it already moved on reports where it stands to its new owner at once.
void FakeWebView::setNavigationHandler(NavigationHandler handler) {
    m_navigation = std::move(handler);

    if (!m_history.empty()) {
        report();
    }
}

void FakeWebView::setOpenHandler(OpenHandler handler) {
    m_record->openers.push_back(std::move(handler));
}

void FakeWebView::setPopupHandler(PopupHandler handler) {
    m_record->popupers.push_back(std::move(handler));
}

void FakeWebView::setCloseHandler(CloseHandler handler) {
    m_record->closers.push_back(std::move(handler));
}

void FakeWebView::setDownloadHandler(DownloadHandler handler) {
    m_record->downloaders.push_back(std::move(handler));
}

void FakeWebView::setPermissionHandler(PermissionHandler handler) {
    m_record->askers.push_back(std::move(handler));
}

void FakeWebView::setFailureHandler(FailureHandler handler) {
    m_record->failers.push_back(std::move(handler));
}

void FakeWebView::answerPermission(std::uint64_t request, bool allowed) {
    m_record->answers.emplace_back(request, allowed);
}

void FakeWebView::report() {
    if (m_navigation) {
        m_navigation({m_history[m_position], "Page " + std::to_string(m_position + 1), m_record->icon, m_record->holdLoading, m_position > 0, m_position + 1 < m_history.size()});
    }
}

} // namespace workpane::tests
