#pragma once

#include "Error.h"
#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/NativeWebView.h"
#include "ui/model/CommonProperties.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"

#include <imgui_internal.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace workpane::ui {

// A native web page placed over its rectangle, hidden while a dialog is open because a native view is drawn above the product surface.
// It reports where its page is, an address the page asks to open in a new tab, a window the page opened and the page asking to close, and leaves each of them to its owner.
// A window a page opened is shown by another web view that names it, so the page keeps talking to the window it opened.
// A native page that could not be built, at once or in the background, leaves a translated message in its place and is reported once.
class WebView final : public Component {
  public:
    explicit WebView(NodeId id);

    [[nodiscard]] std::string_view kind() const override;
    [[nodiscard]] Result<void> command(RenderContext& context, std::string_view name, const json::Json& arguments) override;
    void detach(RenderContext& context) override;
    void update(RenderContext& context) override;

  protected:
    [[nodiscard]] Alignment defaultRowAlignment() const override;
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] Result<void> validate() const override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    static constexpr float webViewMinimumHeight{200.0F};
    static constexpr std::int64_t largestPopup{9007199254740991};

    [[nodiscard]] static bool webAddress(std::string_view url);
    [[nodiscard]] static std::string_view failureKey(std::string_view code);
    [[nodiscard]] bool ensureView(RenderContext& context);
    [[nodiscard]] Result<std::unique_ptr<NativeWebView>> adopt(RenderContext& context) const;
    void load(RenderContext& context);
    void showFailure(RenderContext& context, const Error& error);

    std::string m_url;
    std::string m_html;
    std::int64_t m_popup{0};
    bool m_contentChanged{true};
    std::unique_ptr<NativeWebView> m_view;
    std::string m_failure;
    bool m_failureReported{false};
    std::shared_ptr<std::deque<std::pair<std::string, json::Json>>> m_pending{std::make_shared<std::deque<std::pair<std::string, json::Json>>>()};
    std::shared_ptr<std::optional<Error>> m_buildFailure{std::make_shared<std::optional<Error>>()};
};

} // namespace workpane::ui
