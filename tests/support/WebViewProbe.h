#pragma once

#include "Error.h"
#include "Result.h"
#include "platform/TrackedWebView.h"
#include "platform/WebViewTracker.h"
#include "ui/WebNavigation.h"

#include <imgui_internal.h>

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace workpane::tests {

// A tracked web view without a page, which records every rectangle it was given and every set of holes cut out of it.
// A test plays its page through it, reporting navigations, posting what the page script posts, opening windows and asking to close.
class WebViewProbe final : public platform::TrackedWebView {
  public:
    explicit WebViewProbe(platform::WebViewTracker& tracker);

    [[nodiscard]] Result<void> navigate(std::string_view url) override;
    [[nodiscard]] Result<void> setHtml(std::string_view html) override;
    [[nodiscard]] Result<void> evaluate(std::string_view script) override;
    void reload() override;
    void back() override;
    void forward() override;
    void stop() override;

    void arrive(ui::WebNavigation navigation);
    void post(std::string_view message);
    [[nodiscard]] bool openWindow();
    void askToClose();
    void download(ui::WebDownload download);
    void question(std::string_view url, bool camera, bool microphone, std::function<void(bool allowed)> answer);
    void forgetQuestions();
    void breakDown(Error error);

    std::vector<ImRect> placements;
    std::vector<std::vector<ImRect>> cuts;

  protected:
    void apply(const ImRect& bounds, bool visible) override;
    void cut(ImVec2 size, const std::vector<ImRect>& holes) override;
};

} // namespace workpane::tests
