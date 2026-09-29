#include "Error.h"
#include "support/InterfaceHarness.h"
#include "support/Resources.h"
#include "support/WebViewRecord.h"
#include "ui/NativeViewHost.h"
#include "ui/Texture.h"
#include "ui/TextureCache.h"
#include "ui/WheelScale.h"
#include "ui/model/Component.h"
#include "ui/model/RenderContext.h"
#include "ui/model/SurfaceStore.h"
#include "ui/model/UiEvent.h"
#include "ui/theme/Style.h"
#include "ui/theme/Theme.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace workpane::ui {

using nlohmann::json;

class InteractionTest : public ::testing::Test {
  protected:
    static json node(std::uint64_t id, std::string kind, json props = json::object(), std::vector<json> children = {}) {
        return {{"id", id}, {"kind", std::move(kind)}, {"props", std::move(props)}, {"children", std::move(children)}};
    }

    // Mounts one control at the top left corner of the display and answers the center of the size it measured.
    ImVec2 place(json control) {
        const auto mounted = m_harness.mount(node(1, "column", {{"padding", 0}}, {std::move(control)}));
        EXPECT_TRUE(mounted.hasValue()) << (mounted.hasValue() ? "" : mounted.error().code + " " + mounted.error().detail);
        m_harness.frames(2);
        const ImVec2 size = m_harness.node(2)->measure(m_harness.context(), tests::InterfaceHarness::width);
        const bool stretched = m_harness.node(2)->columnAlignment() == Alignment::Stretch;

        return {stretched ? tests::InterfaceHarness::width / 2.0F : size.x / 2.0F, size.y / 2.0F};
    }

    // Mounts one control against the right edge of the display, at its top or its bottom, and answers the center of the size it measured.
    ImVec2 corner(json control, bool bottom) {
        const auto mounted = m_harness.mount(node(1, "column", {{"padding", 0}, {"justify", bottom ? "end" : "start"}}, {node(3, "row", {{"justify", "end"}}, {std::move(control)})}));
        EXPECT_TRUE(mounted.hasValue()) << (mounted.hasValue() ? "" : mounted.error().code + " " + mounted.error().detail);
        m_harness.frames(2);
        const ImVec2 size = m_harness.node(2)->measure(m_harness.context(), tests::InterfaceHarness::width);

        return {tests::InterfaceHarness::width - size.x / 2.0F, bottom ? tests::InterfaceHarness::height - size.y / 2.0F : size.y / 2.0F};
    }

    static bool inside(const ImGuiWindow& window) {
        return window.Pos.x >= 0.0F && window.Pos.y >= 0.0F && window.Pos.x + window.Size.x <= tests::InterfaceHarness::width && window.Pos.y + window.Size.y <= tests::InterfaceHarness::height;
    }

    // Counts what the last frame drew, which tells two drawings of the same control apart without reading pixels.
    static int vertices() {
        const ImDrawData* data = ImGui::GetDrawData();
        int count = 0;

        for (const ImDrawList* list : data->CmdLists) {
            count += list->VtxBuffer.Size;
        }

        return count;
    }

    std::optional<UiEvent> event(std::string_view name) {
        for (auto& candidate : m_harness.takeEvents()) {
            if (candidate.name == name) {
                return candidate;
            }
        }

        return std::nullopt;
    }

    tests::InterfaceHarness m_harness;
};

TEST_F(InteractionTest, AButtonReportsAClickAndADisabledOneDoesNot) {
    const ImVec2 center = place(node(2, "button", {{"text", "Press"}}));
    m_harness.click(center);
    const auto clicked = event("click");

    ASSERT_TRUE(clicked.has_value());
    EXPECT_EQ(clicked->node, 2U);
    EXPECT_EQ(clicked->surface, tests::InterfaceHarness::surface);

    ASSERT_TRUE(m_harness.patch(2, {{"enabled", false}}).hasValue());
    m_harness.click(center);
    EXPECT_FALSE(event("click").has_value());
}

// Resting the pointer on a component shows its tooltip after the delay, disabled or not, never while the keyboard navigates, and never stops the product.
TEST_F(InteractionTest, AComponentShowsItsTooltipWhenThePointerRests) {
    const ImVec2 center = place(node(2, "button", {{"text", "Hover"}, {"tooltip", "Explains the button"}, {"enabled", false}}));
    m_harness.moveTo(center);
    m_harness.frames(60);

    const ImGuiWindow* tooltip = ImGui::FindWindowByName("##Tooltip_00");
    ASSERT_NE(tooltip, nullptr);
    EXPECT_TRUE(tooltip->Active);

    // While the keyboard navigates, the pointer resting there shows nothing until it moves again.
    GImGui->NavHighlightItemUnderNav = true;
    m_harness.frames(60);
    EXPECT_FALSE(tooltip->Active);
    m_harness.moveTo(ImVec2(center.x + 1.0F, center.y));
    m_harness.frames(60);
    EXPECT_TRUE(tooltip->Active);
}

// A checkbox and a toggle report their new check and name it as the property the event changed, which their node on the Lua side takes.
TEST_F(InteractionTest, ACheckboxAndAToggleReportTheirNewState) {
    m_harness.click(place(node(2, "checkbox", {{"text", "Remember"}})));
    const auto checked = event("change");
    ASSERT_TRUE(checked.has_value());
    EXPECT_EQ(checked->value["checked"], true);
    EXPECT_EQ(checked->state, json({{"checked", "checked"}}));

    ASSERT_TRUE(m_harness.surfaces().unmount(tests::InterfaceHarness::surface, "test", m_harness.context()).hasValue());
    m_harness.click(place(node(2, "toggle", {{"checked", true}})));
    const auto toggled = event("change");
    ASSERT_TRUE(toggled.has_value());
    EXPECT_EQ(toggled->value["checked"], false);
}

TEST_F(InteractionTest, ATextFieldReportsWhatTheReaderTypesAndSubmits) {
    m_harness.click(place(node(2, "textField", {{"placeholder", "Name"}, {"width", 300}})));
    m_harness.type("Workpane");
    const auto changed = m_harness.takeEvents();

    ASSERT_FALSE(changed.empty());
    EXPECT_EQ(changed.back().name, "change");
    EXPECT_EQ(changed.back().value["value"], "Workpane");

    m_harness.press(ImGuiKey_Enter);
    const auto submitted = m_harness.takeEvents();
    ASSERT_FALSE(submitted.empty());
    EXPECT_EQ(submitted.back().name, "submit");
    EXPECT_EQ(submitted.back().value["value"], "Workpane");

    // Leaving the field without Enter finishes the edit with blur instead, after a click far enough from the first one not to count as a double click.
    m_harness.click(ImVec2(250.0F, 10.0F));
    m_harness.type("!");
    m_harness.click(ImVec2(900.0F, 400.0F));
    const auto blurred = event("blur");
    ASSERT_TRUE(blurred.has_value());
    EXPECT_EQ(blurred->value["value"], "Workpane!");
}

// A field reached with the keyboard draws the accent border of the product once, and ImGui draws no ring of its own outside it.
TEST_F(InteractionTest, AFieldReachedWithTheKeyboardDrawsOneBorder) {
    std::ignore = place(node(2, "textField", {{"placeholder", "Name"}, {"width", 300}}));
    m_harness.press(ImGuiKey_Tab);
    m_harness.frames(2);
    const ImU32 accent = m_harness.context().color(ThemeColor::Accent).packed();
    std::optional<ImRect> drawn;

    ASSERT_TRUE(GImGui->NavCursorVisible);

    for (const ImDrawList* list : ImGui::GetDrawData()->CmdLists) {
        for (const ImDrawVert& vertex : list->VtxBuffer) {
            if (vertex.col != accent) {
                continue;
            }

            drawn = drawn.has_value() ? ImRect(ImMin(drawn->Min, vertex.pos), ImMax(drawn->Max, vertex.pos)) : ImRect(vertex.pos, vertex.pos);
        }
    }

    // The border reaches one point past the field on each side for its smoothing, and a ring of ImGui would lie three points out.
    ASSERT_TRUE(drawn.has_value());
    EXPECT_LE(drawn->GetHeight(), m_harness.context().metric(ThemeMetric::ControlHeight) + 2.5F);
}

// A text area, a combo and a date field take the keyboard when asked, from a button that had it, and a component without a control refuses the request.
TEST_F(InteractionTest, AControlTakesTheKeyboardWhenAsked) {
    const json options = json::array({{{"value", "a"}, {"text", "Alpha"}}, {{"value", "b"}, {"text", "Beta"}}});
    ASSERT_TRUE(m_harness.mount(node(1, "column", {{"padding", 0}, {"spacing", 8}}, {node(2, "textArea", {{"rows", 3}}), node(3, "combo", {{"value", "a"}, {"options", options}}), node(4, "dateTimeField", {{"mode", "date"}}), node(5, "button", {{"text", "Save"}}), node(6, "label", {{"text", "Plain"}})})).hasValue());
    m_harness.frames(2);

    // The button is pressed from the keyboard first, so the keyboard has to leave it for the text area.
    m_harness.press(ImGuiKey_Tab);
    m_harness.press(ImGuiKey_Tab);
    m_harness.press(ImGuiKey_Tab);
    m_harness.press(ImGuiKey_Tab);
    m_harness.press(ImGuiKey_Space);
    std::ignore = m_harness.takeEvents();
    ASSERT_TRUE(m_harness.node(2)->command(m_harness.context(), "focus", json::object()).hasValue());
    m_harness.frames(3);
    m_harness.type("note");
    const auto changed = event("change");
    ASSERT_TRUE(changed.has_value());
    EXPECT_EQ(changed->node, 2U);
    EXPECT_EQ(changed->value["value"], "note");

    // A combo with the keyboard opens on Space, and a date field too.
    for (const std::uint64_t control : {3U, 4U}) {
        ASSERT_TRUE(m_harness.node(control)->command(m_harness.context(), "focus", json::object()).hasValue());
        m_harness.frames(3);
        ImGui::SetNavCursorVisible(true);
        m_harness.press(ImGuiKey_Space);
        m_harness.frames(2);
        EXPECT_FALSE(GImGui->OpenPopupStack.empty()) << control;
        m_harness.press(ImGuiKey_Escape);
        m_harness.frames(2);
    }

    EXPECT_EQ(m_harness.node(6)->command(m_harness.context(), "focus", json::object()).error().code, "ui_command_unknown");
    EXPECT_FALSE(m_harness.node(2)->command(m_harness.context(), "focus", {{"selectAll", true}}).hasValue());
}

// A field asked for the keyboard with its text selected replaces what it holds with what is typed next, after it lost the keyboard and while it still has it.
TEST_F(InteractionTest, ATextFieldAskedToSelectItsTextReplacesItWithWhatIsTyped) {
    ASSERT_TRUE(m_harness.mount(node(1, "column", {{"padding", 0}, {"spacing", 8}}, {node(2, "textField", {{"width", 300}}), node(3, "button", {{"text", "Save"}})})).hasValue());
    m_harness.frames(2);
    ASSERT_TRUE(m_harness.node(2)->command(m_harness.context(), "focus", json::object()).hasValue());
    m_harness.frames(3);
    m_harness.type("first");
    m_harness.press(ImGuiKey_Tab);
    std::ignore = m_harness.takeEvents();

    // The field edited before keeps its state inside ImGui, which would otherwise put the caret back where it was.
    for (const std::string_view text : {"second", "third"}) {
        ASSERT_TRUE(m_harness.node(2)->command(m_harness.context(), "focus", {{"selectAll", true}}).hasValue());
        m_harness.frames(3);
        m_harness.type(text);
        const auto changes = m_harness.takeEvents();
        ASSERT_FALSE(changes.empty());
        EXPECT_EQ(changes.back().name, "change");
        EXPECT_EQ(changes.back().value["value"], nlohmann::json(text));
    }

    // A value its plugin replaces in the same frame as the request is selected whole as well.
    ASSERT_TRUE(m_harness.patch(2, {{"value", "replaced by the plugin"}}).hasValue());
    ASSERT_TRUE(m_harness.node(2)->command(m_harness.context(), "focus", {{"selectAll", true}}).hasValue());
    m_harness.frames(3);
    m_harness.type("fourth");
    const auto changes = m_harness.takeEvents();
    ASSERT_FALSE(changes.empty());
    EXPECT_EQ(changes.back().value["value"], "fourth");
}

// A read-only field whose value its plugin replaces after the reader clicked into it keeps drawing, because such a field reads its value afresh every frame instead of reloading an edit it never had.
TEST_F(InteractionTest, AReadOnlyFieldTakesTheValueItsPluginReplaces) {
    const ImVec2 center = place(node(2, "textField", {{"value", "short"}, {"readOnly", true}, {"width", 300}}));
    m_harness.click(center);
    ASSERT_TRUE(m_harness.patch(2, {{"value", "a value much longer than the one before it"}}).hasValue());
    m_harness.frames(3);
    m_harness.click(center);
    m_harness.frames(2);
    EXPECT_FALSE(event("change").has_value());
}

// A field inside a disabled scope draws no clear button, whatever the item drawn before it was, so the reader is never offered to empty a locked value.
TEST_F(InteractionTest, ADisabledTextFieldDrawsNoClearButton) {
    const ImVec2 center = place(node(2, "textField", {{"value", "Workpane"}, {"enabled", false}, {"width", 300}}));
    const int plain = vertices();

    ASSERT_TRUE(m_harness.surfaces().unmount(tests::InterfaceHarness::surface, "test", m_harness.context()).hasValue());
    std::ignore = place(node(2, "textField", {{"value", "Workpane"}, {"clearButton", true}, {"enabled", false}, {"width", 300}}));
    EXPECT_EQ(vertices(), plain);

    m_harness.click(ImVec2(center.x * 2.0F - 14.0F * m_harness.context().scale(), center.y));
    EXPECT_FALSE(event("change").has_value());
}

// The page reports where it is after every move of its history, and a page asking for a new window reaches the owner instead of opening one.
TEST_F(InteractionTest, AWebViewReportsItsNavigationAndThePagesAskingForAWindow) {
    std::ignore = place(node(2, "webView", {{"url", "https://example.com/first"}, {"height", 200}}));
    const auto first = event("navigation");

    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->value["url"], "https://example.com/first");
    EXPECT_EQ(first->value["title"], "Page 1");
    EXPECT_EQ(first->value["canGoBack"], false);

    Component& view = *m_harness.node(2);
    ASSERT_TRUE(view.command(m_harness.context(), "navigate", {{"url", "https://example.com/second"}}).hasValue());
    m_harness.frame();
    EXPECT_EQ(event("navigation")->value["canGoBack"], true);

    ASSERT_TRUE(view.command(m_harness.context(), "back", json::object()).hasValue());
    m_harness.frame();
    const auto returned = event("navigation");
    ASSERT_TRUE(returned.has_value());
    EXPECT_EQ(returned->value["url"], "https://example.com/first");
    EXPECT_EQ(returned->value["canGoForward"], true);

    ASSERT_TRUE(view.command(m_harness.context(), "reload", json::object()).hasValue());
    ASSERT_TRUE(view.command(m_harness.context(), "stop", json::object()).hasValue());
    EXPECT_FALSE(view.command(m_harness.context(), "stop", {{"now", true}}).hasValue());
    EXPECT_EQ(m_harness.webViews().commands, (std::vector<std::string>{"back", "reload", "stop"}));

    ASSERT_FALSE(m_harness.webViews().openers.empty());
    m_harness.webViews().openers.back()("https://example.com/background", true);
    m_harness.frame();
    const auto requested = event("open-request");
    ASSERT_TRUE(requested.has_value());
    EXPECT_EQ(requested->value["url"], "https://example.com/background");
    EXPECT_EQ(requested->value["background"], true);
}

// A file a page downloaded reaches the owner with its path written with forward slashes, whether it finished or failed with the reason the engine gave.
TEST_F(InteractionTest, AWebViewReportsTheFilesItsPagesDownload) {
    std::ignore = place(node(2, "webView", {{"url", "https://example.com/files"}, {"height", 200}}));
    ASSERT_FALSE(m_harness.webViews().downloaders.empty());
    const std::filesystem::path saved = std::filesystem::path("downloads") / "report.pdf";

    m_harness.webViews().downloaders.back()({saved, true, ""});
    m_harness.frame();
    const auto finished = event("download");
    ASSERT_TRUE(finished.has_value());
    EXPECT_EQ(finished->value["path"], "downloads/report.pdf");
    EXPECT_EQ(finished->value["finished"], true);

    m_harness.webViews().downloaders.back()({saved, false, "The network went away"});
    m_harness.frame();
    const auto failed = event("download");
    ASSERT_TRUE(failed.has_value());
    EXPECT_EQ(failed->value["finished"], false);
    EXPECT_EQ(failed->value["message"], "The network went away");
}

// A page asking for the camera or the microphone reaches the owner under a number, which the owner answers with a command, and a command without a valid number or answer is refused.
TEST_F(InteractionTest, AWebViewPassesThePagesAskingForTheCameraToItsOwner) {
    std::ignore = place(node(2, "webView", {{"url", "https://meet.example/room"}, {"height", 200}}));
    ASSERT_FALSE(m_harness.webViews().askers.empty());

    m_harness.webViews().askers.back()({4, "https://meet.example", true, false});
    m_harness.frame();
    const auto asked = event("permission-request");
    ASSERT_TRUE(asked.has_value());
    EXPECT_EQ(asked->value, json({{"request", 4}, {"origin", "https://meet.example"}, {"camera", true}, {"microphone", false}}));

    Component& view = *m_harness.node(2);
    ASSERT_TRUE(view.command(m_harness.context(), "answer-permission", {{"request", 4}, {"allowed", true}}).hasValue());
    EXPECT_EQ(m_harness.webViews().answers, (std::vector<std::pair<std::uint64_t, bool>>{{4, true}}));
    EXPECT_EQ(view.command(m_harness.context(), "answer-permission", {{"request", 0}, {"allowed", true}}).error().code, "json_field_range");
    EXPECT_EQ(view.command(m_harness.context(), "answer-permission", {{"request", 4}}).error().code, "json_field_missing");
}

// A window a page opens waits under a number until a web view naming it shows it, a page asking to close tells the owner, and the icon of a page travels with its navigation.
TEST_F(InteractionTest, AWebViewHandsTheWindowsItsPagesOpenToAnotherWebView) {
    m_harness.webViews().icon = "data:image/png;base64,iVBORw0K";
    std::ignore = place(node(2, "webView", {{"url", "https://example.com/opener"}, {"height", 200}}));
    const auto first = event("navigation");
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->value["icon"], "data:image/png;base64,iVBORw0K");

    auto window = m_harness.context().nativeViews().createWebView();
    ASSERT_TRUE(window.hasValue());
    ASSERT_TRUE(window.value()->navigate("https://accounts.example/sign-in").hasValue());
    m_harness.webViews().popupers.front()(std::move(window.value()));
    m_harness.frame();
    const auto opened = event("popup");
    ASSERT_TRUE(opened.has_value());
    const std::uint64_t popup = opened->value["popup"];

    const int created = m_harness.webViews().created;
    ASSERT_TRUE(m_harness.surfaces().unmount(tests::InterfaceHarness::surface, "test", m_harness.context()).hasValue());
    const auto mounted = m_harness.mount(node(1, "column", {{"padding", 0}}, {node(3, "webView", {{"popup", popup}, {"height", 200}})}));
    ASSERT_TRUE(mounted.hasValue()) << mounted.error().code;
    m_harness.frames(2);
    const auto adopted = event("navigation");
    ASSERT_TRUE(adopted.has_value());
    EXPECT_EQ(adopted->value["url"], "https://accounts.example/sign-in");
    EXPECT_EQ(m_harness.webViews().created, created);

    m_harness.webViews().closers.back()();
    m_harness.frame();
    const auto closing = event("close-request");
    ASSERT_TRUE(closing.has_value());
    EXPECT_EQ(closing->node, NodeId{3});
    EXPECT_EQ(m_harness.patch(3, {{"url", "https://example.com/"}}).error().code, "webview_content_ambiguous");

    // Navigating the window leaves what the web view declared as it was, so a later patch of another property is still whole.
    ASSERT_TRUE(m_harness.node(3)->command(m_harness.context(), "navigate", {{"url", "https://accounts.example/done"}}).hasValue());
    EXPECT_TRUE(m_harness.patch(3, {{"height", 240}}).hasValue());
}

// A web view opens only web, file and about addresses, and a page behind another tab reports as soon as one on screen does.
TEST_F(InteractionTest, AWebViewOpensOnlyItsAddressesAndReportsFromBehindAnotherTab) {
    EXPECT_EQ(m_harness.mount(node(1, "column", json::object(), {node(2, "webView", {{"url", "javascript:alert(1)"}})})).error().code, "webview_address_invalid");
    const auto mounted = m_harness.mount(node(1, "stack", {{"current", 1}}, {node(2, "label", {{"text", "Other"}}), node(3, "webView", {{"url", "https://example.com/"}, {"height", 200}})}));
    ASSERT_TRUE(mounted.hasValue());
    m_harness.frames(2);
    EXPECT_EQ(m_harness.node(3)->command(m_harness.context(), "navigate", {{"url", "ftp://example.com/"}}).error().code, "webview_address_invalid");
    ASSERT_TRUE(m_harness.node(3)->command(m_harness.context(), "navigate", {{"url", "about:blank"}}).hasValue());

    ASSERT_TRUE(m_harness.patch(1, {{"current", 0}}).hasValue());
    m_harness.frames(2);
    std::ignore = m_harness.takeEvents();
    m_harness.webViews().openers.back()("https://example.com/later", true);
    m_harness.frame();
    const auto requested = event("open-request");
    ASSERT_TRUE(requested.has_value());
    EXPECT_EQ(requested->value["url"], "https://example.com/later");
}

// A window no web view adopts within a few frames is closed, and a web view naming it later reports it gone.
TEST_F(InteractionTest, AWindowNobodyAdoptsIsClosed) {
    std::ignore = place(node(2, "webView", {{"url", "https://example.com/opener"}, {"height", 200}}));
    auto window = m_harness.context().nativeViews().createWebView();
    ASSERT_TRUE(window.hasValue());
    m_harness.webViews().popupers.front()(std::move(window.value()));
    m_harness.frame();
    const auto opened = event("popup");
    ASSERT_TRUE(opened.has_value());
    m_harness.frames(12);

    const auto mounted = m_harness.mount(node(1, "column", {{"padding", 0}}, {node(3, "webView", {{"popup", opened->value["popup"]}, {"height", 200}})}));
    ASSERT_TRUE(mounted.hasValue());
    m_harness.frame();
    const auto failed = event("error");
    ASSERT_TRUE(failed.has_value());
    EXPECT_EQ(failed->value["code"], "webview_popup_missing");
}

// A page built in the background that could not be built gives its place to the failure, which reaches the owner once, and the web view answers no command afterwards.
TEST_F(InteractionTest, AWebViewReportsOnceThatItsPageCouldNotBeBuilt) {
    std::ignore = place(node(2, "webView", {{"url", "https://example.com/"}, {"height", 200}}));
    ASSERT_FALSE(m_harness.webViews().failers.empty());
    Component& view = *m_harness.node(2);
    ASSERT_TRUE(view.command(m_harness.context(), "reload", json::object()).hasValue());
    const Error refused{"webview_create_failed", "WebView2 could not build the page", "HRESULT 0x80070002"};

    m_harness.webViews().failers.back()(refused);
    m_harness.webViews().failers.back()(refused);
    m_harness.frame();
    const auto failed = event("error");
    ASSERT_TRUE(failed.has_value());
    EXPECT_EQ(failed->value["code"], "webview_create_failed");
    EXPECT_EQ(view.command(m_harness.context(), "reload", json::object()).error().code, "webview_unavailable");

    m_harness.webViews().failers.back()(refused);
    m_harness.frames(2);
    EXPECT_FALSE(event("error").has_value());
}

// A tab shows the picture of a data address in the square of an icon once it decoded, and refuses an image beside an icon or one that is no data address.
TEST_F(InteractionTest, ATabShowsTheImageItIsGiven) {
    const std::string pixel = "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8DwHwAFBQIAX8jx0gAAAABJRU5ErkJggg==";
    std::ignore = place(node(2, "tabs", {{"items", json::array({{{"id", "a"}, {"text", "Page"}, {"image", pixel}}})}, {"current", "a"}}));
    const auto& texture = m_harness.context().textures().requestData(pixel);

    for (int attempt = 0; attempt < 200 && texture.state == TextureState::Loading; ++attempt) {
        m_harness.frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    ASSERT_EQ(texture.state, TextureState::Ready);
    EXPECT_EQ(texture.width, 1);
    m_harness.frame();
    bool drawn = false;

    for (const ImDrawList* list : ImGui::GetDrawData()->CmdLists) {
        for (const ImDrawCmd& command : list->CmdBuffer) {
            drawn = drawn || command.TexRef._TexData == texture.reference()._TexData;
        }
    }

    EXPECT_TRUE(drawn);
    EXPECT_EQ(m_harness.patch(2, {{"items", json::array({{{"id", "a"}, {"text", "Page"}, {"image", pixel}, {"icon", "browser"}}})}}).error().code, "ui_tab_image_invalid");
    EXPECT_EQ(m_harness.patch(2, {{"items", json::array({{{"id", "a"}, {"text", "Page"}, {"image", "https://example.com/icon.png"}}})}}).error().code, "ui_tab_image_invalid");
}

// A short tab held over a wider neighbour moves only once the pointer would fall inside it there, so the two never trade places back and forth under a resting pointer.
TEST_F(InteractionTest, AShortTabHeldOverAWiderOneStaysInPlace) {
    const json items = json::array({{{"id", "a"}, {"text", "A"}}, {{"id", "b"}, {"text", "A tab whose title is long enough to be far wider than the others"}}});
    std::ignore = place(node(2, "tabs", {{"items", items}, {"current", "a"}, {"movable", true}}));
    const float tab = m_harness.context().metric(ThemeMetric::TabMinimumWidth);
    const float middle = m_harness.context().metric(ThemeMetric::WorkspaceBarHeight) / 2.0F;
    std::ignore = m_harness.takeEvents();

    m_harness.moveTo(ImVec2(tab * 0.5F, middle));
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    m_harness.frame();

    for (int step = 1; step <= 5; ++step) {
        m_harness.moveTo(ImVec2(tab * (0.5F + static_cast<float>(step) * 0.2F), middle));
    }

    m_harness.frames(6);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    m_harness.frame();
    EXPECT_FALSE(event("move").has_value());
}

// A tab dragged along a movable strip takes the place of the tab under the pointer, and its page moves with it.
TEST_F(InteractionTest, AMovableTabFollowsThePointerWithItsPage) {
    const json items = json::array({{{"id", "a"}, {"text", "A"}}, {{"id", "b"}, {"text", "B"}}, {{"id", "c"}, {"text", "C"}}});
    std::ignore = place(node(2, "tabs", {{"items", items}, {"current", "a"}, {"movable", true}}, {node(3, "label", {{"text", "Page A"}}), node(4, "label", {{"text", "Page B"}}), node(5, "label", {{"text", "Page C"}})}));
    const float tab = m_harness.context().metric(ThemeMetric::TabMinimumWidth);
    const float middle = m_harness.context().metric(ThemeMetric::WorkspaceBarHeight) / 2.0F;

    m_harness.drag(ImVec2(tab * 0.5F, middle), ImVec2(tab * 2.5F, middle));
    std::vector<UiEvent> moves;

    for (auto& candidate : m_harness.takeEvents()) {
        if (candidate.name == "move") {
            moves.push_back(candidate);
        }
    }

    ASSERT_FALSE(moves.empty());
    EXPECT_EQ(moves.back().value["id"], "a");
    EXPECT_EQ(moves.back().value["index"], 2);
    EXPECT_EQ(m_harness.node(2)->children().back()->id(), 3U);

    // The move names the items in their new order as the property it changed and the new order of the pages.
    EXPECT_EQ(moves.back().state, json({{"items", "items"}}));
    EXPECT_EQ(moves.back().value["items"][2]["id"], "a");
    EXPECT_EQ(moves.back().order, (std::vector<NodeId>{4, 5, 3}));

    // A strip that is not movable keeps its order whatever the pointer does.
    ASSERT_TRUE(m_harness.patch(2, {{"movable", false}}).hasValue());
    m_harness.drag(ImVec2(tab * 2.5F, middle), ImVec2(tab * 0.5F, middle));
    EXPECT_FALSE(event("move").has_value());

    // A double click on a tab activates it, which is how a plugin offers to rename it.
    m_harness.click(ImVec2(tab * 0.5F, middle));
    m_harness.click(ImVec2(tab * 0.5F, middle));
    const auto activated = event("activate");
    ASSERT_TRUE(activated.has_value());
    EXPECT_EQ(activated->value["id"], "b");
}

// A text area that submits on Enter reports its value and keeps the keyboard, while Shift and Enter still start a new line, and a value replaced by its plugin is the one it edits next.
TEST_F(InteractionTest, ATextAreaSubmitsOnEnterAndBreaksLinesWithShiftAndEnter) {
    m_harness.click(place(node(2, "textArea", {{"submitOnEnter", true}, {"rows", 3}})));
    m_harness.type("hello");
    std::ignore = m_harness.takeEvents();

    m_harness.press(ImGuiKey_Enter);
    const auto submitted = event("submit");
    ASSERT_TRUE(submitted.has_value());
    EXPECT_EQ(submitted->value["value"], "hello");
    EXPECT_NE(ImGui::GetActiveID(), 0U);

    m_harness.press(ImGuiMod_Shift | ImGuiKey_Enter);
    m_harness.type("world");
    const auto changes = m_harness.takeEvents();
    ASSERT_FALSE(changes.empty());
    EXPECT_EQ(changes.back().name, "change");
    EXPECT_EQ(changes.back().value["value"], "hello\nworld");

    // Without the property Enter starts a new line as in any text area.
    ASSERT_TRUE(m_harness.patch(2, {{"submitOnEnter", false}}).hasValue());
    m_harness.press(ImGuiKey_Enter);
    const auto plain = m_harness.takeEvents();
    ASSERT_FALSE(plain.empty());
    EXPECT_EQ(plain.back().value["value"], "hello\nworld\n");

    // A value the plugin replaces while the reader is typing, such as a composer emptied after sending, is what the area edits next.
    ASSERT_TRUE(m_harness.patch(2, {{"value", ""}}).hasValue());
    m_harness.frame();
    m_harness.type("again");
    const auto emptied = m_harness.takeEvents();
    ASSERT_FALSE(emptied.empty());
    EXPECT_EQ(emptied.back().value["value"], "again");
}

// A scroll area that follows keeps its bottom in view while the content grows, stops following once the reader scrolls up and reports reaching the top.
TEST_F(InteractionTest, AScrollAreaFollowsGrowingContentAndReportsTheTop) {
    json lines = json::array();

    for (int line = 0; line < 40; ++line) {
        lines.push_back("Line " + std::to_string(line));
    }

    const std::string text = lines[0].get<std::string>() + "\n" + lines[1].get<std::string>();
    const auto mounted = m_harness.mount(node(1, "column", {{"padding", 0}}, {node(2, "scroll", {{"follow", true}, {"height", 120}}, {node(3, "markdown", {{"text", text}, {"breaks", true}})})}));
    ASSERT_TRUE(mounted.hasValue());
    m_harness.frames(3);
    // clang-format off
    const auto scrolled = []() { for (ImGuiWindow* window : GImGui->Windows) { if (std::string_view(window->Name).find("##scroll") != std::string_view::npos) { return window; } } return static_cast<ImGuiWindow*>(nullptr); };
    // clang-format on
    std::string grown;

    for (const auto& line : lines) {
        grown += line.get<std::string>() + "\n";
    }

    ASSERT_TRUE(m_harness.patch(3, {{"text", grown}}).hasValue());
    m_harness.frames(4);
    ASSERT_NE(scrolled(), nullptr);
    EXPECT_GT(scrolled()->ScrollMax.y, 0.0F);
    EXPECT_FLOAT_EQ(scrolled()->Scroll.y, scrolled()->ScrollMax.y);
    std::ignore = m_harness.takeEvents();

    // Scrolling to the top reports it once, and content that grows afterwards leaves the reader where they are.
    m_harness.moveTo(ImVec2(100.0F, 60.0F));
    ImGui::GetIO().AddMouseWheelEvent(0.0F, 400.0F);
    m_harness.frames(4);
    EXPECT_FLOAT_EQ(scrolled()->Scroll.y, 0.0F);
    EXPECT_TRUE(event("top").has_value());
    ASSERT_TRUE(m_harness.patch(3, {{"text", grown + "Another line\nAnd one more"}}).hasValue());
    m_harness.frames(4);
    EXPECT_FLOAT_EQ(scrolled()->Scroll.y, 0.0F);
    EXPECT_FALSE(event("top").has_value());
}

// A double click on a card outside its controls activates the innermost card under the pointer, and one on a control inside it activates nothing.
TEST_F(InteractionTest, ACardReportsActivateOnADoubleClickOutsideItsControls) {
    const json inner = node(3, "card", {{"height", 120}}, {node(4, "button", {{"text", "Run"}})});
    const auto mounted = m_harness.mount(node(1, "column", {{"padding", 0}}, {node(2, "card", {{"height", 300}}, {inner})}));
    ASSERT_TRUE(mounted.hasValue());
    m_harness.frames(3);

    m_harness.click(ImVec2(400.0F, 100.0F));
    m_harness.click(ImVec2(400.0F, 100.0F));
    const auto activated = event("activate");
    ASSERT_TRUE(activated.has_value());
    EXPECT_EQ(activated->node, 3U);

    m_harness.click(ImVec2(400.0F, 250.0F));
    m_harness.click(ImVec2(400.0F, 250.0F));
    const auto outer = event("activate");
    ASSERT_TRUE(outer.has_value());
    EXPECT_EQ(outer->node, 2U);

    m_harness.click(ImVec2(40.0F, 35.0F));
    m_harness.click(ImVec2(40.0F, 35.0F));
    const auto pressed = m_harness.takeEvents();
    // clang-format off
    EXPECT_TRUE(std::ranges::none_of(pressed, [](const UiEvent& candidate) { return candidate.name == "activate"; }));
    EXPECT_TRUE(std::ranges::any_of(pressed, [](const UiEvent& candidate) { return candidate.name == "click"; }));
    // clang-format on
}

// A popover opens its panel below the button, a swatch inside it reports the press, and the plugin closes the panel with a command.
TEST_F(InteractionTest, APopoverShowsItsPanelUntilThePluginClosesIt) {
    const json cells = json::array({{{"column", 0}, {"row", 0}, {"rowSpan", 2}}, {{"column", 1}, {"row", 0}}, {{"column", 1}, {"row", 1}}});
    const json swatch = node(4, "layoutSwatch", {{"columns", 2}, {"rows", 2}, {"cells", cells}});
    const ImVec2 center = place(node(2, "popover", {{"text", "Layout"}, {"icon", "layout"}}, {node(3, "column", {{"width", 120}, {"padding", 0}}, {swatch})}));
    const float bottom = center.y * 2.0F;

    m_harness.click(center);
    m_harness.frames(2);
    EXPECT_TRUE(ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));
    m_harness.click(ImVec2(24.0F, bottom + 24.0F));
    EXPECT_TRUE(event("click").has_value());

    ASSERT_TRUE(m_harness.node(2)->command(m_harness.context(), "close", json::object()).hasValue());
    m_harness.frames(2);
    EXPECT_FALSE(ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));

    // A popover at the right edge opens its panel moved left, so the panel stays inside the window.
    const json edge = node(5, "popover", {{"text", "Edge"}}, {node(6, "column", {{"width", 300}, {"height", 40}})});
    const auto mounted = m_harness.mount(node(1, "row", {{"justify", "end"}, {"padding", 0}}, {edge}));
    ASSERT_TRUE(mounted.hasValue()) << mounted.error().code << " " << mounted.error().detail;
    m_harness.frames(2);
    ASSERT_TRUE(m_harness.node(5)->command(m_harness.context(), "open", json::object()).hasValue());
    m_harness.frames(3);
    ASSERT_FALSE(GImGui->OpenPopupStack.empty());
    EXPECT_LE(GImGui->OpenPopupStack.back().Window->Rect().Max.x, tests::InterfaceHarness::width);
    EXPECT_GE(GImGui->OpenPopupStack.back().Window->Rect().Min.x, 0.0F);
    std::ignore = place(node(2, "popover", {{"text", "Layout"}}, {node(3, "column", {{"width", 120}, {"padding", 0}}, {swatch})}));

    // A popover holds exactly one child, and a swatch refuses a cell outside its grid.
    EXPECT_EQ(m_harness.patch(4, {{"cells", json::array({{{"column", 2}, {"row", 0}}})}}).error().code, "ui_swatch_cell_invalid");
    EXPECT_EQ(m_harness.patch(4, {{"cells", json::array()}}).error().code, "ui_swatch_cell_invalid");
    EXPECT_EQ(m_harness.surfaces().replaceChildren(tests::InterfaceHarness::surface, "test", 2, json::array(), json::object(), m_harness.context()).error().code, "ui_popover_children");
}

TEST_F(InteractionTest, AListReportsTheRowTheReaderSelects) {
    const ImVec2 center = place(node(2, "list", {{"items", json::array({{{"id", "first"}, {"text", "First"}}, {{"id", "second"}, {"text", "Second"}}})}}));
    m_harness.click(ImVec2(center.x, center.y * 1.5F));
    const auto selected = event("select");

    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected->value["id"], "second");
}

// The row the reader chose is remembered by the surface, so a patch the plugin makes afterwards is proven against what the reader left on screen.
TEST_F(InteractionTest, ProvesAPatchAgainstTheRowTheReaderChose) {
    const ImVec2 center = place(node(2, "list", {{"items", json::array({{{"id", "first"}, {"text", "First"}}, {{"id", "second"}, {"text", "Second"}}})}, {"selected", "first"}}));
    m_harness.click(ImVec2(center.x, center.y * 1.5F));
    ASSERT_TRUE(event("select").has_value());

    EXPECT_TRUE(m_harness.patch(2, {{"items", json::array({{{"id", "second"}, {"text", "Second"}}, {{"id", "third"}, {"text", "Third"}}})}}).hasValue());
    EXPECT_EQ(m_harness.patch(2, {{"items", json::array({{{"id", "third"}, {"text", "Third"}}})}}).error().code, "ui_item_unknown");
}

// A slider draws the value its plugin gave without moving it, and a number refuses bounds that form no range, a value outside them, a step its decimals cannot write and a bound or a value they cannot write.
TEST_F(InteractionTest, ANumberKeepsTheValueItWasGivenAndRefusesImpossibleSteps) {
    const ImVec2 center = place(node(2, "slider", {{"value", 0.3}, {"minimum", 0}, {"maximum", 1}, {"step", 0.1}, {"decimals", 1}}));
    m_harness.frames(3);
    EXPECT_FALSE(event("change").has_value());

    EXPECT_EQ(m_harness.patch(2, {{"minimum", 2}}).error().code, "ui_number_bounds");
    EXPECT_EQ(m_harness.patch(2, {{"maximum", 0.2}}).error().code, "ui_number_range");
    EXPECT_EQ(m_harness.patch(2, {{"step", 0.05}}).error().code, "ui_number_step");
    EXPECT_EQ(m_harness.mount(node(1, "column", json::object(), {node(2, "numberField", {{"value", 1}, {"step", 0.25}})})).error().code, "ui_number_step");

    // A bound or a value its decimals cannot write is refused, since the slider would round it outside its range or away from what was given.
    EXPECT_EQ(m_harness.patch(2, {{"maximum", 0.95}}).error().code, "ui_number_decimals");
    EXPECT_EQ(m_harness.patch(2, {{"value", 0.35}}).error().code, "ui_number_decimals");

    // A value the reader chose is written with the decimals of the slider.
    std::ignore = place(node(2, "slider", {{"value", 0.3}, {"minimum", 0}, {"maximum", 1}, {"step", 0.1}, {"decimals", 1}}));
    m_harness.click(ImVec2(center.x * 0.7F, center.y));
    const auto moved = event("change");
    ASSERT_TRUE(moved.has_value());
    const double value = moved->value["value"];
    EXPECT_EQ(value, std::round(value * 10.0) / 10.0);
}

// A menu button reports the item picked without owning a selection, so a patch of its items after a pick is still whole.
TEST_F(InteractionTest, AMenuButtonTakesNewItemsAfterAPick) {
    const ImVec2 center = place(node(2, "menuButton", {{"text", "Menu"}, {"items", json::array({{{"id", "copy"}, {"text", "Copy"}}, {{"id", "paste"}, {"text", "Paste"}}})}}));
    m_harness.click(center);
    m_harness.frames(3);
    ASSERT_FALSE(GImGui->OpenPopupStack.empty());
    const ImGuiWindow* popup = GImGui->OpenPopupStack.back().Window;
    ASSERT_NE(popup, nullptr);
    m_harness.click(ImVec2(popup->Pos.x + popup->Size.x / 2.0F, popup->Pos.y + popup->WindowPadding.y + 8.0F * m_harness.context().scale()));
    const auto picked = event("select");

    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(picked->value["item"], "copy");
    EXPECT_TRUE(m_harness.patch(2, {{"items", json::array({{{"id", "cut"}, {"text", "Cut"}}})}}).hasValue());
}

// A strip with pages changes its tabs and its pages in one step, and a patch or a set of pages that leaves a tab without its page is refused whole.
TEST_F(InteractionTest, ATabStripChangesItsTabsAndItsPagesInOneStep) {
    const json items = json::array({{{"id", "a"}, {"text", "A"}}, {{"id", "b"}, {"text", "B"}}});
    const auto mounted = m_harness.mount(node(1, "tabs", {{"items", items}, {"current", "b"}}, {node(2, "label", {{"text", "Page A"}}), node(3, "label", {{"text", "Page B"}})}));
    ASSERT_TRUE(mounted.hasValue());
    m_harness.frames(2);

    json grown = items;
    grown.push_back({{"id", "c"}, {"text", "C"}});
    EXPECT_EQ(m_harness.patch(1, {{"items", grown}, {"current", "c"}}).error().code, "ui_tabs_pages");
    ASSERT_TRUE(m_harness.surfaces().replaceChildren(tests::InterfaceHarness::surface, "test", 1, json::array({{{"id", 2}, {"kept", true}}, {{"id", 3}, {"kept", true}}, node(4, "label", {{"text", "Page C"}})}), {{"items", grown}, {"current", "c"}}, m_harness.context()).hasValue());
    m_harness.frames(2);

    const json alone = json::array({{{"id", "a"}, {"text", "A"}}});
    EXPECT_EQ(m_harness.surfaces().replaceChildren(tests::InterfaceHarness::surface, "test", 1, json::array({{{"id", 2}, {"kept", true}}}), {{"items", alone}, {"current", "c"}}, m_harness.context()).error().code, "ui_tabs_current");
    EXPECT_NE(m_harness.node(3), nullptr);
    EXPECT_NE(m_harness.node(4), nullptr);
    ASSERT_TRUE(m_harness.surfaces().replaceChildren(tests::InterfaceHarness::surface, "test", 1, json::array({{{"id", 2}, {"kept", true}}}), {{"items", alone}, {"current", "a"}}, m_harness.context()).hasValue());
    EXPECT_EQ(m_harness.node(3), nullptr);
    EXPECT_NE(m_harness.node(2), nullptr);
}

// A selectable text is as tall as its lines, so the field that lets the reader select it never shows a scroll bar.
TEST_F(InteractionTest, ASelectableTextShowsNoScrollBar) {
    std::ignore = place(node(2, "label", {{"text", "This text can be selected and copied"}, {"selectable", true}}));
    m_harness.frames(2);
    bool found = false;

    for (const ImGuiWindow* window : GImGui->Windows) {
        if (window->Active && std::string_view(window->Name).find("##label") != std::string_view::npos) {
            found = true;
            EXPECT_FALSE(window->ScrollbarY) << window->Name;
        }
    }

    EXPECT_TRUE(found);
}

// A canvas draws the list its plugin sends inside itself, and refuses a list with a broken command whole, naming that command, so the list on screen stays.
TEST_F(InteractionTest, ACanvasDrawsTheListItsPluginSendsAndRefusesABrokenOne) {
    ASSERT_TRUE(m_harness.mount(node(1, "column", {{"padding", 0}}, {node(2, "canvas", {{"height", 100}})})).hasValue());
    m_harness.frames(2);
    const int empty = vertices();
    // clang-format off
    const auto draw = [this](const json& commands) { return m_harness.surfaces().command(tests::InterfaceHarness::surface, "test", 2, "draw", {{"commands", commands}}, m_harness.context()); };
    // clang-format on

    const json list = json::array({{{"op", "rect"}, {"x", 4}, {"y", 4}, {"width", 20}, {"height", 10}, {"color", "accent"}}, {{"op", "rect"}, {"x", 4}, {"y", 4}, {"width", 20}, {"height", 10}, {"color", "#ff8800"}, {"filled", false}, {"thickness", 2}, {"opacity", 0.5}}, {{"op", "line"}, {"x1", 0}, {"y1", 0}, {"x2", 50}, {"y2", 50}, {"color", "text"}}, {{"op", "circle"}, {"x", 30}, {"y", 30}, {"radius", 8}, {"color", "danger"}}, {{"op", "clip"}, {"x", 0}, {"y", 0}, {"width", 40}, {"height", 40}}, {{"op", "text"}, {"x", 10}, {"y", 60}, {"text", "Score"}, {"color", "text"}, {"size", 18}, {"align", "center"}}, {{"op", "text"}, {"x", 10}, {"y", 80}, {"text", {{"key", "workpane.actions.ok"}}}, {"color", "#123456"}, {"face", "monospace"}}, {{"op", "unclip"}}});
    ASSERT_TRUE(draw(list).hasValue());
    m_harness.frames(2);
    const int drawn = vertices();
    EXPECT_GT(drawn, empty);

    const std::vector<std::pair<json, std::string>> broken{{json::array({list[0], {{"op", "star"}, {"x", 1}, {"y", 1}}}), "canvas.commands[1]"}, {json::array({{{"op", "rect"}, {"x", 1}, {"y", 1}, {"width", 2}, {"height", 2}, {"color", "sparkle"}}}), "canvas.commands[0].color"}, {json::array({{{"op", "rect"}, {"x", 1}, {"y", 1}, {"width", -2}, {"height", 2}, {"color", "text"}}}), "canvas.commands[0]"}, {json::array({{{"op", "line"}, {"x", 1}, {"y", 1}, {"color", "text"}}}), "canvas.commands[0]"}, {json::array({{{"op", "image"}, {"image", "../secret.png"}, {"x", 1}, {"y", 1}, {"width", 2}, {"height", 2}}}), "canvas.commands[0].image"}, {json::array({{{"op", "text"}, {"x", 1}, {"y", 1}, {"text", "A"}, {"color", "text"}, {"size", 400}}}), "canvas.commands[0]"}, {json::array({{{"op", "unclip"}, {"extra", true}}}), "canvas.commands[0]"}, {json::array({{{"op", "image"}, {"image", "a.png"}, {"x", 0}, {"y", 0}, {"width", 20}, {"height", 20}, {"tile", {{"width", 10}, {"height", 10}}}, {"rotation", 1}}}), "canvas.commands[0].tile"}, {json::array({{{"op", "image"}, {"image", "a.png"}, {"x", 0}, {"y", 0}, {"width", 1000}, {"height", 1000}, {"tile", {{"width", 1}, {"height", 1}}}}}), "canvas.commands[0].tile"}, {json::array({{{"op", "image"}, {"image", "a.png"}, {"x", 0}, {"y", 0}, {"width", 20}, {"height", 20}, {"tile", {{"width", 0}, {"height", 10}}}}}), "canvas.commands[0].tile.width"}};

    for (const auto& [commands, detail] : broken) {
        const auto refused = draw(commands);
        ASSERT_FALSE(refused.hasValue()) << commands.dump();
        EXPECT_EQ(refused.error().code, "ui_canvas_command_invalid") << commands.dump();
        EXPECT_NE(refused.error().detail.find(detail), std::string::npos) << refused.error().detail;
    }

    EXPECT_EQ(draw(json::array_t(20001, json{{"op", "unclip"}})).error().code, "ui_canvas_list_too_long");
    EXPECT_EQ(m_harness.surfaces().command(tests::InterfaceHarness::surface, "test", 2, "paint", json::object(), m_harness.context()).error().code, "ui_command_unknown");
    m_harness.frames(2);
    EXPECT_EQ(vertices(), drawn);
}

// A tiled picture repeats at the size of its tile across the rectangle of its command, from its corner, and is cut at the far edges.
TEST_F(InteractionTest, ACanvasRepeatsATiledPicture) {
    ASSERT_TRUE(m_harness.mount(node(1, "column", {{"padding", 0}}, {node(2, "canvas", {{"height", 100}})})).hasValue());
    const std::string image = "plugins/task-hero/assets/terrain/sky.png";
    const json tiled = json::array({{{"op", "image"}, {"image", image}, {"x", -4}, {"y", 0}, {"width", 100}, {"height", 25}, {"tile", {{"width", 10}, {"height", 10}}}}});
    ASSERT_TRUE(m_harness.surfaces().command(tests::InterfaceHarness::surface, "test", 2, "draw", {{"commands", tiled}}, m_harness.context()).hasValue());
    const auto& texture = m_harness.context().textures().request(tests::Resources::staged() / image);

    for (int attempt = 0; attempt < 200 && texture.state == TextureState::Loading; ++attempt) {
        m_harness.frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    ASSERT_EQ(texture.state, TextureState::Ready);
    m_harness.frame();
    int corners = 0;

    for (const ImDrawList* list : ImGui::GetDrawData()->CmdLists) {
        for (const ImDrawCmd& command : list->CmdBuffer) {
            corners += command.TexRef._TexData == texture.reference()._TexData ? static_cast<int>(command.ElemCount) : 0;
        }
    }

    EXPECT_EQ(corners, 10 * 3 * 6);
}

// A picture declaring more pixels than the product draws is refused from its header before a pixel is decoded, and a picture no drawn frame asked for in a while leaves the cache and is decoded again when it is asked for.
TEST_F(InteractionTest, BoundsAndReleasesThePicturesItDraws) {
    TextureCache& textures = m_harness.context().textures();
    // A PNG header declaring sixteen thousand pixels square, which stb accepts, followed only by the start of its data.
    const std::string huge = "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAPoAAAD6ACAYAAAAAAAAAAAAAAElEQVQ=";
    const std::filesystem::path sky = tests::Resources::staged() / "plugins/task-hero/assets/terrain/sky.png";
    // clang-format off
    const auto settle = [this](const Texture& texture) {
        for (int attempt = 0; attempt < 200 && texture.state == TextureState::Loading; ++attempt) {
            m_harness.frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };
    // clang-format on

    const Texture& refused = textures.requestData(huge);
    settle(refused);
    EXPECT_EQ(refused.state, TextureState::Failed);
    EXPECT_NE(refused.failure.find("larger"), std::string::npos) << refused.failure;

    const Texture& drawn = textures.request(sky);
    settle(drawn);
    ASSERT_EQ(drawn.state, TextureState::Ready);
    m_harness.frames(700);

    EXPECT_EQ(textures.request(sky).state, TextureState::Loading);
    EXPECT_EQ(textures.requestData(huge).state, TextureState::Loading);
}

// A canvas holds the pictures its plugin names from the frame it mounts, so each one is ready before the first frame that draws it and stays while the canvas does, and lets go of them when its plugin names others or the canvas leaves.
TEST_F(InteractionTest, ACanvasKeepsThePicturesItNamesReadyWhileItIsMounted) {
    TextureCache& textures = m_harness.context().textures();
    const std::string sky = "plugins/task-hero/assets/terrain/sky.png";
    const std::string coin = "plugins/task-hero/assets/loot/coin.png";
    const std::filesystem::path skyFile = tests::Resources::staged() / sky;
    const std::filesystem::path coinFile = tests::Resources::staged() / coin;
    ASSERT_TRUE(m_harness.mount(node(1, "column", {{"padding", 0}}, {node(2, "canvas", {{"height", 100}, {"pictures", json::array({sky, sky})}})})).hasValue());

    for (int attempt = 0; attempt < 200 && textures.request(skyFile).state == TextureState::Loading; ++attempt) {
        m_harness.frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    ASSERT_EQ(textures.request(skyFile).state, TextureState::Ready);

    // No frame draws the picture, and it stays while the canvas holds it.
    m_harness.frames(700);
    EXPECT_EQ(textures.request(skyFile).state, TextureState::Ready);

    // A picture the plugin no longer names leaves with the next frame, and the new one is decoded at once.
    ASSERT_TRUE(m_harness.patch(2, {{"pictures", json::array({coin})}}).hasValue());
    m_harness.frames(2);
    EXPECT_EQ(textures.request(skyFile).state, TextureState::Loading);

    for (int attempt = 0; attempt < 200 && textures.request(coinFile).state == TextureState::Loading; ++attempt) {
        m_harness.frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    ASSERT_EQ(textures.request(coinFile).state, TextureState::Ready);

    // A canvas that leaves, as the band of a plugin turned off does, lets go of what it held.
    ASSERT_TRUE(m_harness.mount(node(3, "column", {{"padding", 0}})).hasValue());
    m_harness.frames(2);
    EXPECT_EQ(textures.request(coinFile).state, TextureState::Loading);

    // A picture outside the assets of the plugin, or too many of them, refuses the canvas.
    const auto outside = m_harness.mount(node(4, "canvas", {{"pictures", json::array({"../secret.png"})}}));
    ASSERT_FALSE(outside.hasValue());
    EXPECT_EQ(outside.error().code, "ui_canvas_picture_invalid");
    const auto many = m_harness.mount(node(5, "canvas", {{"pictures", json(std::vector<std::string>(257, sky))}}));
    ASSERT_FALSE(many.hasValue());
    EXPECT_EQ(many.error().code, "ui_canvas_pictures_too_many");
}

// A frame of a sheet is sampled a sliver of a pixel inside its edges in a pixelated canvas and half a pixel inside in any other, mirrored or not, so the frame beside it never shows along an edge, while a whole picture keeps every pixel.
TEST_F(InteractionTest, ACanvasNeverSamplesTheFrameBesideTheOneItDraws) {
    ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
    // clang-format off
    platform.DrawCallback_SetSamplerNearest = [](const ImDrawList*, const ImDrawCmd*) {};
    platform.DrawCallback_SetSamplerLinear = [](const ImDrawList*, const ImDrawCmd*) {};
    // clang-format on
    ASSERT_TRUE(m_harness.mount(node(1, "column", {{"padding", 0}}, {node(2, "canvas", {{"height", 160}, {"pixelated", true}})})).hasValue());
    const std::string image = "plugins/task-hero/assets/units/blue-lancer-run.png";
    const json frame = {{"op", "image"}, {"image", image}, {"x", 3.5}, {"y", 0}, {"width", 36}, {"height", 74.5}, {"source", {{"x", 72}, {"y", 0}, {"width", 72}, {"height", 149}}}};
    json mirrored = frame;
    mirrored["flipX"] = true;
    const auto& texture = m_harness.context().textures().request(tests::Resources::staged() / image);
    // clang-format off
    const auto sampled = [&](const json& commands) {
        EXPECT_TRUE(m_harness.surfaces().command(tests::InterfaceHarness::surface, "test", 2, "draw", {{"commands", commands}}, m_harness.context()).hasValue());
        m_harness.frame();
        ImVec2 least(FLT_MAX, FLT_MAX);
        ImVec2 most(-FLT_MAX, -FLT_MAX);

        for (const ImDrawList* list : ImGui::GetDrawData()->CmdLists) {
            for (const ImDrawCmd& command : list->CmdBuffer) {
                if (command.TexRef._TexData != texture.reference()._TexData) {
                    continue;
                }

                for (unsigned int index = 0; index < command.ElemCount; ++index) {
                    const ImVec2 uv = list->VtxBuffer[static_cast<int>(list->IdxBuffer[static_cast<int>(command.IdxOffset + index)])].uv;
                    least = ImVec2(std::min(least.x, uv.x * static_cast<float>(texture.width)), std::min(least.y, uv.y * static_cast<float>(texture.height)));
                    most = ImVec2(std::max(most.x, uv.x * static_cast<float>(texture.width)), std::max(most.y, uv.y * static_cast<float>(texture.height)));
                }
            }
        }

        return std::pair<ImVec2, ImVec2>(least, most);
    };
    // clang-format on

    for (int attempt = 0; attempt < 200 && texture.state == TextureState::Loading; ++attempt) {
        m_harness.frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    ASSERT_EQ(texture.state, TextureState::Ready);

    for (const json& drawn : {frame, mirrored}) {
        const auto [least, most] = sampled(json::array({drawn}));
        EXPECT_NEAR(least.x, 72.0F + 1.0F / 64.0F, 1e-3F);
        EXPECT_NEAR(most.x, 144.0F - 1.0F / 64.0F, 1e-3F);
        EXPECT_NEAR(least.y, 1.0F / 64.0F, 1e-3F);
        EXPECT_NEAR(most.y, 149.0F - 1.0F / 64.0F, 1e-3F);
    }

    ASSERT_TRUE(m_harness.patch(2, {{"pixelated", false}}).hasValue());
    const auto [linearLeast, linearMost] = sampled(json::array({frame}));
    EXPECT_FLOAT_EQ(linearLeast.x, 72.5F);
    EXPECT_FLOAT_EQ(linearMost.x, 143.5F);

    json whole = frame;
    whole.erase("source");
    const auto [wholeLeast, wholeMost] = sampled(json::array({whole}));
    EXPECT_FLOAT_EQ(wholeLeast.x, 0.0F);
    EXPECT_FLOAT_EQ(wholeMost.x, static_cast<float>(texture.width));
    platform.DrawCallback_SetSamplerNearest = nullptr;
    platform.DrawCallback_SetSamplerLinear = nullptr;
}

// Curves are tessellated and edges smoothed in pixels of the framebuffer, so a display of density two draws a circle with more segments over a fringe of half a point.
TEST_F(InteractionTest, ShapesFollowTheDensityOfTheFramebuffer) {
    // clang-format off
    const auto circle = [](float density) {
        ui::Style::matchDensity(density);
        ImDrawList list(ImGui::GetDrawListSharedData());
        list._ResetForNewFrame();
        list.PushClipRectFullScreen();
        list.AddCircleFilled(ImVec2(40.0F, 40.0F), 10.0F, IM_COL32_WHITE);

        return std::pair<int, float>(list.VtxBuffer.Size, list._FringeScale);
    };
    // clang-format on

    const auto [single, singleFringe] = circle(1.0F);
    const auto [dense, denseFringe] = circle(2.0F);
    EXPECT_FLOAT_EQ(singleFringe, 1.0F);
    EXPECT_FLOAT_EQ(denseFringe, 0.5F);
    EXPECT_GT(dense, single);
    EXPECT_FLOAT_EQ(ImGui::GetStyle().CircleTessellationMaxError, 0.1F);
    ui::Style::matchDensity(1.0F);
}

// A canvas with a frame rate tells its plugin each tick with the seconds since the last one and asks the loop for the next tick, a hidden canvas never ticks and a canvas without a rate never ticks.
TEST_F(InteractionTest, ACanvasTicksAtItsFrameRate) {
    ASSERT_TRUE(m_harness.mount(node(1, "column", {{"padding", 0}}, {node(2, "canvas", {{"height", 100}, {"frameRate", 10}})})).hasValue());
    std::vector<json> ticks;

    for (int frame = 0; frame < 60; ++frame) {
        m_harness.frame();

        for (const auto& event : m_harness.takeEvents()) {
            if (event.name == "frame") {
                ticks.push_back(event.value);
            }
        }
    }

    ASSERT_GE(ticks.size(), 10U);
    ASSERT_LE(ticks.size(), 11U);
    EXPECT_EQ(ticks[0]["delta"], 0.0);
    EXPECT_FLOAT_EQ(ticks[0]["width"].get<float>(), tests::InterfaceHarness::width);
    EXPECT_FLOAT_EQ(ticks[0]["height"].get<float>(), 100.0F);

    for (std::size_t index = 1; index < ticks.size(); ++index) {
        EXPECT_NEAR(ticks[index]["delta"].get<double>(), 0.1, 0.02) << index;
    }

    EXPECT_LT(m_harness.context().frameDeadline(), ticks.back()["time"].get<double>() + 0.12);

    // A canvas that is not drawn reports nothing, and the first tick after it returns tells at most a quarter of a second.
    ASSERT_TRUE(m_harness.patch(2, {{"visible", false}}).hasValue());
    m_harness.frames(90);
    // clang-format off
    EXPECT_TRUE(std::ranges::none_of(m_harness.takeEvents(), [](const auto& event) { return event.name == "frame"; }));
    // clang-format on
    ASSERT_TRUE(m_harness.patch(2, {{"visible", true}}).hasValue());
    m_harness.frame();
    const auto resumed = event("frame");
    ASSERT_TRUE(resumed.has_value());
    EXPECT_LE(resumed->value["delta"].get<double>(), 0.25);
    EXPECT_GT(resumed->value["delta"].get<double>(), 0.2);

    ASSERT_TRUE(m_harness.patch(2, {{"frameRate", 0}}).hasValue());
    m_harness.frames(20);

    for (const auto& event : m_harness.takeEvents()) {
        EXPECT_NE(event.name, "frame");
    }

    EXPECT_FALSE(m_harness.patch(2, {{"frameRate", 121}}).hasValue());
}

// A canvas tells the pointer of the reader in points from its corner and how far the wheel scrolls in points, takes the keyboard on a click when it may and then tells every key by the name the shortcuts use.
TEST_F(InteractionTest, ACanvasReportsThePointerAndTheKeysOfItsReader) {
    ASSERT_TRUE(m_harness.mount(node(1, "column", {{"padding", 0}}, {node(2, "canvas", {{"height", 100}, {"focusable", true}, {"tracking", true}})})).hasValue());
    m_harness.frames(2);
    std::ignore = m_harness.takeEvents();

    m_harness.moveTo(ImVec2(120.0F, 40.0F));
    const auto moved = event("pointer-move");
    ASSERT_TRUE(moved.has_value());
    EXPECT_FLOAT_EQ(moved->value["x"].get<float>(), 120.0F);
    EXPECT_FLOAT_EQ(moved->value["y"].get<float>(), 40.0F);

    // A turn toward the bottom and the right reads as positive distances.
    const ImVec2 turn = ui::WheelScale::units(ImVec2(-1.0F, -1.0F), 30.0F);
    ImGui::GetIO().AddMouseWheelEvent(turn.x, turn.y);
    m_harness.frame();
    const auto wheeled = event("wheel");
    ASSERT_TRUE(wheeled.has_value());
    EXPECT_NEAR(wheeled->value["deltaX"].get<double>(), 30.0, 1e-3);
    EXPECT_NEAR(wheeled->value["deltaY"].get<double>(), 30.0, 1e-3);

    m_harness.click(ImVec2(120.0F, 40.0F));
    std::vector<std::string> names;

    for (const auto& happened : m_harness.takeEvents()) {
        names.push_back(happened.name + (happened.value.contains("button") ? ":" + happened.value["button"].get<std::string>() : "") + (happened.value.contains("focused") ? ":" + std::string(happened.value["focused"].get<bool>() ? "in" : "out") : ""));
    }

    EXPECT_NE(std::ranges::find(names, "pointer-down:left"), names.end());
    EXPECT_NE(std::ranges::find(names, "pointer-up:left"), names.end());
    EXPECT_NE(std::ranges::find(names, "focus:in"), names.end());

    m_harness.press(ImGuiKey_Space);
    std::vector<std::string> keys;

    for (const auto& happened : m_harness.takeEvents()) {
        if (happened.name == "key-down" || happened.name == "key-up") {
            keys.push_back(happened.name + ":" + happened.value["key"].get<std::string>());
        }
    }

    EXPECT_EQ(keys, (std::vector<std::string>{"key-down:space", "key-up:space"}));

    // A key still held when the canvas loses the keyboard is let go of before the canvas tells it lost the keyboard.
    ImGui::GetIO().AddKeyEvent(ImGuiKey_A, true);
    m_harness.frame();
    std::ignore = m_harness.takeEvents();
    m_harness.click(ImVec2(120.0F, 400.0F));
    ImGui::GetIO().AddKeyEvent(ImGuiKey_A, false);
    m_harness.frame();
    std::vector<std::string> ending;

    for (const auto& happened : m_harness.takeEvents()) {
        if (happened.name == "key-up" || happened.name == "focus") {
            ending.push_back(happened.name + ":" + (happened.name == "focus" ? std::string(happened.value["focused"].get<bool>() ? "in" : "out") : happened.value["key"].get<std::string>()));
        }
    }

    EXPECT_EQ(ending, (std::vector<std::string>{"key-up:a", "focus:out"}));
}

// A pixelated canvas asks the renderer for nearest sampling before its pictures and gives it linear sampling back after them, and any other canvas asks for nothing.
TEST_F(InteractionTest, APixelatedCanvasSamplesItsPicturesByTheNearestPixel) {
    ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
    static std::vector<std::string> samplers;
    // The callbacks run as a renderer runs them, so each sampler the draw lists ask for is recorded in its order.
    // clang-format off
    platform.DrawCallback_SetSamplerNearest = [](const ImDrawList*, const ImDrawCmd*) { samplers.emplace_back("nearest"); };
    platform.DrawCallback_SetSamplerLinear = [](const ImDrawList*, const ImDrawCmd*) { samplers.emplace_back("linear"); };
    const auto callbacks = []() {
        samplers.clear();

        for (const ImDrawList* list : ImGui::GetDrawData()->CmdLists) {
            for (const ImDrawCmd& command : list->CmdBuffer) {
                if (command.UserCallback != nullptr) {
                    command.UserCallback(list, &command);
                }
            }
        }

        return samplers;
    };
    // clang-format on

    ASSERT_TRUE(m_harness.mount(node(1, "column", {{"padding", 0}}, {node(2, "canvas", {{"height", 100}, {"pixelated", true}})})).hasValue());
    m_harness.frames(2);
    EXPECT_EQ(callbacks(), (std::vector<std::string>{"nearest", "linear"}));

    ASSERT_TRUE(m_harness.patch(2, {{"pixelated", false}}).hasValue());
    m_harness.frames(2);
    EXPECT_TRUE(callbacks().empty());
    platform.DrawCallback_SetSamplerNearest = nullptr;
    platform.DrawCallback_SetSamplerLinear = nullptr;
}

// A secret narrower than its eye leaves its field out, since ImGui would stretch a field of no width across the rest of the window.
TEST_F(InteractionTest, AFieldWithoutRoomTakesNoClickBesideIt) {
    ASSERT_TRUE(m_harness.mount(node(1, "column", {{"padding", 0}}, {node(2, "secretField", {{"value", "token-1234"}, {"maxWidth", 20}, {"align", "start"}})})).hasValue());
    m_harness.frames(2);

    m_harness.click(ImVec2(tests::InterfaceHarness::width / 2.0F, m_harness.context().metric(ThemeMetric::ControlHeight) / 2.0F));
    m_harness.frames(2);

    EXPECT_FALSE(ImGui::GetIO().WantTextInput);
}

// A scroll squeezed to no width by its neighbour opens no child window, since ImGui would size one of no width to the rest of the window.
TEST_F(InteractionTest, AComponentWithoutRoomDrawsNothing) {
    ASSERT_TRUE(m_harness.mount(node(1, "row", {{"padding", 0}, {"spacing", 0}}, {node(2, "label", {{"text", "Wide"}, {"minWidth", tests::InterfaceHarness::width}}), node(3, "scroll", {{"grow", 1}}, {node(4, "label", {{"text", "Inside"}})})})).hasValue());
    m_harness.frames(2);

    for (const ImGuiWindow* window : GImGui->Windows) {
        EXPECT_FALSE(window->Active && std::string_view(window->Name).find("##scroll") != std::string_view::npos) << window->Name;
    }
}

// The eye beside a secret shows it at once and hides it again, a secret that confirms its reveal asks its owner instead, and an edit finishes on Enter or on leaving the field.
TEST_F(InteractionTest, ASecretFieldRevealsItsValueFromItsEye) {
    ASSERT_TRUE(m_harness.mount(node(1, "column", {{"padding", 0}}, {node(2, "secretField", {{"value", "token-1234"}}), node(3, "secretField", {{"value", "api-key"}, {"confirmReveal", true}})})).hasValue());
    m_harness.frames(2);
    const float height = m_harness.context().metric(ThemeMetric::ControlHeight);
    const float eye = tests::InterfaceHarness::width - height / 2.0F;

    m_harness.click(ImVec2(eye, height / 2.0F));
    const auto revealed = event("reveal");
    ASSERT_TRUE(revealed.has_value());
    EXPECT_EQ(revealed->value["revealed"], true);

    m_harness.click(ImVec2(eye, height / 2.0F));
    EXPECT_EQ(event("reveal")->value["revealed"], false);

    m_harness.click(ImVec2(eye, height * 1.5F));
    EXPECT_TRUE(event("reveal-request").has_value());

    // A secret finishes its edit on Enter and on leaving the field, as a text field does, so a setting keeps a key once rather than on every character.
    m_harness.click(ImVec2(40.0F, height / 2.0F));
    m_harness.press(ImGuiKey_End);
    m_harness.type("-5");
    std::ignore = m_harness.takeEvents();
    m_harness.press(ImGuiKey_Enter);
    const auto submitted = event("submit");
    ASSERT_TRUE(submitted.has_value());
    EXPECT_EQ(submitted->value["value"], "token-1234-5");

    m_harness.click(ImVec2(60.0F, height / 2.0F));
    m_harness.press(ImGuiKey_End);
    m_harness.type("6");
    m_harness.click(ImVec2(900.0F, 400.0F));
    const auto blurred = event("blur");
    ASSERT_TRUE(blurred.has_value());
    EXPECT_EQ(blurred->value["value"], "token-1234-56");
}

TEST_F(InteractionTest, AMarkdownLinkReportsItsDestination) {
    place(node(2, "markdown", {{"text", "[Open the site](https://example.com)"}}));
    m_harness.click(ImVec2(30.0F, 8.0F));
    const auto linked = event("link");

    ASSERT_TRUE(linked.has_value());
    EXPECT_EQ(linked->value["url"], "https://example.com");
}

TEST_F(InteractionTest, AChipReportsAClickForItsOwnerToToggle) {
    m_harness.click(place(node(2, "chip", {{"text", "Errors"}})));
    EXPECT_TRUE(event("click").has_value());
}

TEST_F(InteractionTest, AContextMenuOpensUnderThePointerAndReportsTheItemPicked) {
    const json items = json::array({{{"id", "rename"}, {"text", "Rename"}}, {{"separator", true}}, {{"id", "delete"}, {"text", "Delete"}, {"destructive", true}}});
    const ImVec2 center = place(node(2, "card", {{"padding", 24}, {"menu", items}}, {node(3, "label", {{"text", "Right click"}, {"menu", json::array({{{"id", "copy"}, {"text", "Copy"}}})}})}));

    m_harness.rightClick(ImVec2(center.x, 4.0F));
    m_harness.frames(2);
    m_harness.click(ImVec2(center.x + 24.0F, 4.0F + 16.0F));
    const auto picked = event("menu");

    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(picked->node, 2U);
    EXPECT_EQ(picked->value["item"], "rename");

    // The innermost component under the pointer claims the click, so the label opens its own menu rather than the one of the card.
    m_harness.rightClick(ImVec2(40.0F, 30.0F));
    m_harness.frames(2);
    m_harness.click(ImVec2(64.0F, 30.0F + 16.0F));
    const auto copied = event("menu");

    ASSERT_TRUE(copied.has_value());
    EXPECT_EQ(copied->node, 3U);
    EXPECT_EQ(copied->value["item"], "copy");
}

// A draggable item lands inside a droppable item near its middle and between the children of a droppable item near their edges.
TEST_F(InteractionTest, ATreeReportsWhereADraggedItemLands) {
    const json one = {{"id", "b1"}, {"text", "One"}, {"draggable", true}};
    const json two = {{"id", "b2"}, {"text", "Two"}, {"draggable", true}};
    const json three = {{"id", "b3"}, {"text", "Three"}, {"draggable", true}};
    const json loose = {{"id", "loose"}, {"text", "Loose"}, {"droppable", true}, {"expanded", true}, {"children", json::array({one})}};
    const json group = {{"id", "group"}, {"text", "Group"}, {"droppable", true}, {"expanded", true}, {"children", json::array({two, three})}};
    const json items = json::array({loose, group});
    std::ignore = place(node(2, "tree", {{"items", items}}));
    const float row = m_harness.node(2)->measure(m_harness.context(), tests::InterfaceHarness::width).y / 5.0F;
    // clang-format off
    const auto at = [row](int index, float fraction) { return ImVec2(120.0F, row * (static_cast<float>(index) + fraction)); };
    // clang-format on

    m_harness.drag(at(1, 0.5F), at(2, 0.5F));
    auto moved = event("move");
    ASSERT_TRUE(moved.has_value());
    EXPECT_EQ(moved->value, (json{{"item", "b1"}, {"parent", "group"}, {"index", 2}}));

    m_harness.drag(at(4, 0.5F), at(3, 0.1F));
    moved = event("move");
    ASSERT_TRUE(moved.has_value());
    EXPECT_EQ(moved->value, (json{{"item", "b3"}, {"parent", "group"}, {"index", 0}}));

    // An item dropped where it already is, and an item that is not draggable, report nothing.
    m_harness.drag(at(3, 0.5F), at(3, 0.9F));
    EXPECT_FALSE(event("move").has_value());
    m_harness.drag(at(0, 0.5F), at(2, 0.5F));
    EXPECT_FALSE(event("move").has_value());
}

// A dragged value reaches the innermost component under the pointer that accepts its kind, and a click on the source still clicks it.
TEST_F(InteractionTest, ADraggedValueReachesTheInnermostTargetThatAcceptsItsKind) {
    const json card = {{"kind", "sample.card"}, {"value", {{"id", 7}}}, {"label", "Card"}};
    const json accepted = json::array({"sample.card"});
    const json source = node(2, "button", {{"text", "Card"}, {"drag", card}, {"accepts", accepted}, {"width", 120}, {"height", 40}});
    const json other = node(4, "column", {{"accepts", json::array({"sample.other"})}, {"height", 60}});
    const json inner = node(5, "column", {{"accepts", accepted}, {"height", 60}});
    const json outer = node(3, "column", {{"accepts", accepted}, {"height", 160}, {"spacing", 0}, {"padding", 0}}, {other, inner});
    const json plain = node(6, "button", {{"text", "Plain"}, {"width", 120}, {"height", 40}});
    ASSERT_TRUE(m_harness.mount(node(1, "column", {{"padding", 0}, {"spacing", 0}}, {source, outer, plain})).hasValue());
    m_harness.frames(2);
    const json delivered = {{"kind", "sample.card"}, {"value", {{"id", 7}}}};

    m_harness.click(ImVec2(60.0F, 20.0F));
    EXPECT_TRUE(event("click").has_value());

    // A target that accepts another kind passes the drop to the container around it.
    m_harness.drag(ImVec2(60.0F, 20.0F), ImVec2(60.0F, 70.0F));
    auto events = m_harness.takeEvents();
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].name, "drop");
    EXPECT_EQ(events[0].node, 3U);
    EXPECT_EQ(events[0].value, delivered);

    m_harness.drag(ImVec2(60.0F, 20.0F), ImVec2(60.0F, 130.0F));
    events = m_harness.takeEvents();
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].node, 5U);

    // Letting go over a component that accepts nothing, or over the source itself, delivers nothing and clicks nothing.
    m_harness.drag(ImVec2(60.0F, 20.0F), ImVec2(60.0F, 220.0F));
    EXPECT_TRUE(m_harness.takeEvents().empty());
    m_harness.drag(ImVec2(60.0F, 5.0F), ImVec2(60.0F, 35.0F));
    EXPECT_TRUE(m_harness.takeEvents().empty());

    // Escape abandons a drag that is still moving.
    m_harness.moveTo(ImVec2(60.0F, 20.0F));
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    m_harness.frames(1);
    m_harness.moveTo(ImVec2(60.0F, 130.0F));
    m_harness.press(ImGuiKey_Escape);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    m_harness.frames(2);
    EXPECT_TRUE(m_harness.takeEvents().empty());

    // A kind that is not a dotted lowercase name, and a drag without its label, are refused whole.
    EXPECT_EQ(m_harness.patch(2, {{"drag", {{"kind", "Sample Card"}, {"value", 1}, {"label", "Card"}}}}).error().code, "ui_drag_kind_invalid");
    EXPECT_EQ(m_harness.patch(3, {{"accepts", json::array({"sample..card."})}}).error().code, "ui_drag_kind_invalid");
    EXPECT_FALSE(m_harness.patch(2, {{"drag", {{"kind", "sample.card"}, {"value", 1}}}}).hasValue());
}

// A branch whose children are not known yet opens like any other, which is when its plugin loads them, and a revealed item scrolls into view.
TEST_F(InteractionTest, ATreeOpensAnUnloadedBranchAndRevealsAnItem) {
    json items = json::array({{{"id", "folder"}, {"text", "folder"}, {"branch", true}}});

    for (int index = 0; index < 60; ++index) {
        items.push_back({{"id", "file-" + std::to_string(index)}, {"text", "file " + std::to_string(index)}});
    }

    const json tree = node(3, "tree", {{"items", items}});
    ASSERT_TRUE(m_harness.mount(node(1, "column", {{"padding", 0}}, {node(2, "scroll", {{"height", 120}}, {tree})})).hasValue());
    m_harness.frames(2);
    m_harness.click(ImVec2(14.0F, 10.0F));
    const auto opened = event("toggle");
    ASSERT_TRUE(opened.has_value());
    EXPECT_EQ(opened->value, (json{{"id", "folder"}, {"expanded", true}}));

    ASSERT_TRUE(m_harness.node(3)->command(m_harness.context(), "reveal", {{"id", "file-59"}}).hasValue());
    m_harness.frames(3);
    float scrolled = 0.0F;

    for (ImGuiWindow* window : GImGui->Windows) {
        scrolled = std::max(scrolled, window->Scroll.y);
    }

    EXPECT_GT(scrolled, 100.0F);
    EXPECT_FALSE(m_harness.node(3)->command(m_harness.context(), "reveal", json::object()).hasValue());
}

// A secondary click on an item with a menu selects it and opens that menu, which reports the item and the choice together.
TEST_F(InteractionTest, ATreeItemOpensItsOwnMenu) {
    const json menu = json::array({{{"id", "open"}, {"text", "Open"}}});
    const json items = json::array({{{"id", "first"}, {"text", "First"}}, {{"id", "second"}, {"text", "Second"}, {"menu", menu}}});
    std::ignore = place(node(2, "tree", {{"items", items}}));
    const float row = m_harness.node(2)->measure(m_harness.context(), tests::InterfaceHarness::width).y / 2.0F;

    m_harness.rightClick(ImVec2(120.0F, row * 1.5F));
    const auto selected = event("select");
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected->value["id"], "second");

    m_harness.frames(2);
    m_harness.click(ImVec2(144.0F, row * 1.5F + 16.0F));
    const auto picked = event("item-menu");
    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(picked->value, (json{{"id", "second"}, {"item", "open"}}));

    // An item without a menu opens none, and its secondary click leaves the selection alone.
    m_harness.rightClick(ImVec2(120.0F, row * 0.5F));
    m_harness.frames(2);
    EXPECT_FALSE(event("select").has_value());
}

TEST_F(InteractionTest, AMenuRefusesAnItemWithoutItsIdentity) {
    EXPECT_FALSE(m_harness.mount(node(1, "button", {{"menu", json::array({{{"text", "Nameless"}}})}})).hasValue());
}

// A menu and a list open under their control where the window has room, and near its right edge or its bottom they turn left or above, never leaving the window or covering the control.
TEST_F(InteractionTest, PopupsOpenInsideTheWindowNearItsEdges) {
    json items = json::array();
    json options = json::array();

    for (int index = 0; index < 8; ++index) {
        items.push_back({{"id", "item-" + std::to_string(index)}, {"text", "A long menu entry number " + std::to_string(index)}});
        options.push_back({{"value", "option-" + std::to_string(index)}, {"text", "A long option number " + std::to_string(index)}});
    }

    for (const bool bottom : {false, true}) {
        for (const auto& control : {node(2, "menuButton", {{"text", "Menu"}, {"items", items}}), node(2, "combo", {{"options", options}, {"width", 120}})}) {
            const ImVec2 center = corner(control, bottom);
            m_harness.click(center);
            m_harness.frames(3);
            ASSERT_FALSE(GImGui->OpenPopupStack.empty());
            const ImGuiWindow* popup = GImGui->OpenPopupStack.back().Window;
            ASSERT_NE(popup, nullptr);
            EXPECT_TRUE(inside(*popup)) << control["kind"] << (bottom ? " at the bottom" : " at the top");
            EXPECT_FALSE(popup->Rect().Contains(center)) << control["kind"] << (bottom ? " at the bottom " : " at the top ") << popup->Pos.x << "," << popup->Pos.y << " " << popup->Size.x << "x" << popup->Size.y << " center " << center.x << "," << center.y;
            m_harness.press(ImGuiKey_Escape);
            ASSERT_TRUE(m_harness.surfaces().unmount(tests::InterfaceHarness::surface, "test", m_harness.context()).hasValue());
        }
    }
}

// A tooltip is a dark panel of the theme with its text in white, sized to its words and kept inside the window even for a control against its edge.
TEST_F(InteractionTest, ATooltipIsADarkPanelInsideTheWindow) {
    const ImVec2 center = corner(node(2, "button", {{"text", "Hover"}, {"tooltip", "A tooltip long enough to pass the right edge of the window if it were placed at the pointer"}}), false);
    m_harness.moveTo(center);
    m_harness.frames(60);

    const ImGuiWindow* tooltip = nullptr;

    for (const ImGuiWindow* window : GImGui->Windows) {
        if (window->Active && (window->Flags & ImGuiWindowFlags_Tooltip) != 0) {
            tooltip = window;
        }
    }

    ASSERT_NE(tooltip, nullptr);
    EXPECT_TRUE(inside(*tooltip));
    EXPECT_LE(tooltip->ContentSize.x, tooltip->Size.x);
    const ImU32 background = ImGui::ColorConvertFloat4ToU32(m_harness.context().color(ThemeColor::Tooltip).vector());
    const ImU32 ink = ImGui::ColorConvertFloat4ToU32(m_harness.context().color(ThemeColor::OnTooltip).vector());
    // clang-format off
    const auto drawn = [&](ImU32 color) { return std::ranges::any_of(tooltip->DrawList->VtxBuffer, [color](const ImDrawVert& vertex) { return vertex.col == color; }); };
    // clang-format on
    EXPECT_TRUE(drawn(background));
    EXPECT_TRUE(drawn(ink));
}

} // namespace workpane::ui
