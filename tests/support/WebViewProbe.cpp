#include "support/WebViewProbe.h"

#include <utility>

namespace workpane::tests {

WebViewProbe::WebViewProbe(platform::WebViewTracker& tracker) : TrackedWebView(tracker) {}

Result<void> WebViewProbe::navigate(std::string_view) {
    return Result<void>::success();
}

Result<void> WebViewProbe::setHtml(std::string_view) {
    return Result<void>::success();
}

Result<void> WebViewProbe::evaluate(std::string_view) {
    return Result<void>::success();
}

void WebViewProbe::reload() {}

void WebViewProbe::back() {}

void WebViewProbe::forward() {}

void WebViewProbe::stop() {}

void WebViewProbe::arrive(ui::WebNavigation navigation) {
    report(std::move(navigation));
}

void WebViewProbe::post(std::string_view message) {
    receive(message);
}

// A window is opened only while the owner of the view takes windows, as a platform view asks before it builds one.
bool WebViewProbe::openWindow() {
    if (!opensPopups()) {
        return false;
    }

    offer(std::make_unique<WebViewProbe>(tracker()));

    return true;
}

void WebViewProbe::askToClose() {
    closeRequested();
}

void WebViewProbe::download(ui::WebDownload download) {
    downloaded(std::move(download));
}

void WebViewProbe::question(std::string_view url, bool camera, bool microphone, std::function<void(bool allowed)> answer) {
    ask(url, camera, microphone, std::move(answer));
}

void WebViewProbe::forgetQuestions() {
    refuseQuestions();
}

void WebViewProbe::breakDown(Error error) {
    failed(std::move(error));
}

void WebViewProbe::apply(const ImRect& bounds, bool) {
    placements.push_back(bounds);
}

void WebViewProbe::cut(ImVec2, const std::vector<ImRect>& holes) {
    cuts.push_back(holes);
}

} // namespace workpane::tests
