#include "app/Product.h"
#include "persistence/Database.h"
#include "persistence/DatabaseBootstrap.h"
#include "persistence/PreferenceStore.h"
#include "scripting/ScriptRuntime.h"
#include "support/HeadlessProduct.h"
#include "support/RootedPath.h"
#include "support/SurfaceReader.h"
#include "support/TemporaryDirectory.h"
#include "support/WebViewRecord.h"
#include "ui/NativeViewHost.h"
#include "ui/model/RenderContext.h"
#include "ui/model/SurfaceStore.h"
#include "ui/model/UiEvent.h"
#include "ui/shell/DialogHost.h"
#include "ui/shell/Shell.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace workpane::tests {

using nlohmann::json;

// Drives the browser through its toolbar, its tabs and its bookmarks, against a web view that reports every address it is sent to as loaded.
class BrowserTest : public ::testing::Test {
  protected:
    static constexpr std::string_view page{"view:browser:web"};

    [[nodiscard]] const std::filesystem::path& data() const {
        return m_data.path();
    }

    static void boot(HeadlessProduct& product) {
        ASSERT_TRUE(product.boot().hasValue());
        ASSERT_TRUE(product.navigate("browser:web").hasValue());
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return !tabItems(product).empty(); }));
        // clang-format on
    }

    [[nodiscard]] static json tabItems(HeadlessProduct& product) {
        const SurfaceReader reader(product.declared(page));
        const auto tabs = reader.nodes("tabs");
        return tabs.empty() ? json::array() : reader.properties(tabs.front())["items"];
    }

    [[nodiscard]] static std::string current(HeadlessProduct& product) {
        const SurfaceReader reader(product.declared(page));
        const auto tabs = reader.nodes("tabs");
        return tabs.empty() ? std::string() : reader.properties(tabs.front())["current"].get<std::string>();
    }

    static void emit(HeadlessProduct& product, std::string_view surface, std::string_view kind, std::size_t index, std::string_view name, json value) {
        const auto nodes = SurfaceReader(product.declared(surface)).nodes(kind);
        ASSERT_GT(nodes.size(), index) << kind;
        product.emit(surface, nodes[index], name, std::move(value));
    }

    static void click(HeadlessProduct& product, std::string_view surface, std::string_view key) {
        const auto node = SurfaceReader(product.declared(surface)).nodeShowing(key);
        ASSERT_TRUE(node.has_value()) << key;
        product.emit(surface, *node, "click", json::object());
    }

    [[nodiscard]] static std::vector<json> tabs(const HeadlessProduct& product) {
        return product.query("SELECT id, position, title, url, active FROM browser__tabs ORDER BY position");
    }

    [[nodiscard]] static std::vector<json> errors(const HeadlessProduct& product) {
        return product.query("SELECT source, category, message, details_json FROM logs__entries WHERE level = 'error'");
    }

  private:
    TemporaryDirectory m_data;
};

// A window a page opens becomes an active tab showing that window, a tab asked for in the background opens behind the active one, a page asking to close its window closes its tab, and every tab shows the icon of its page.
TEST_F(BrowserTest, OpensTheWindowsAndTheBackgroundTabsPagesAskFor) {
    HeadlessProduct product(data());
    product.webViews().icon = "data:image/png;base64,iVBORw0K";
    boot(product);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return tabItems(product)[0].value("image", "") == "data:image/png;base64,iVBORw0K"; }));
    // clang-format on
    const std::string first = tabItems(product)[0]["id"];

    product.webViews().openers.back()("https://example.org/background", true);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return tabs(product).size() == 2U && tabs(product)[1]["url"] == "https://example.org/background"; }));
    // clang-format on
    EXPECT_EQ(tabs(product)[0]["active"], 1);
    EXPECT_EQ(current(product), first);

    auto window = product.product().render().nativeViews().createWebView();
    ASSERT_TRUE(window.hasValue());
    ASSERT_TRUE(window.value()->navigate("https://accounts.example/sign-in").hasValue());
    const int created = product.webViews().created;
    product.webViews().popupers.front()(std::move(window.value()));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return tabs(product).size() == 3U && tabs(product)[2]["url"] == "https://accounts.example/sign-in"; }));
    // clang-format on
    EXPECT_EQ(tabs(product)[2]["active"], 1);
    EXPECT_EQ(product.webViews().created, created);
    const auto popup = SurfaceReader(product.declared(page)).nodes("webView");
    ASSERT_EQ(popup.size(), 3U);
    EXPECT_TRUE(SurfaceReader(product.declared(page)).properties(popup[2]).contains("popup"));
    EXPECT_FALSE(SurfaceReader(product.declared(page)).properties(popup[2]).contains("url"));

    product.webViews().closers.back()();
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return tabs(product).size() == 2U; }));
    // clang-format on
    EXPECT_EQ(tabs(product)[1]["url"], "https://example.org/background");
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A tab changed while the clock stands before the moment the tab was created keeps its change stamped no earlier than its creation, so the next start reads it again.
TEST_F(BrowserTest, StartsAgainAfterTheClockWasSetBack) {
    {
        HeadlessProduct product(data());
        boot(product);
        product.stop();
    }

    {
        auto database = persistence::Database::open(data() / persistence::DatabaseBootstrap::databaseName);
        ASSERT_TRUE(database.hasValue());
        ASSERT_TRUE(database.value().run("UPDATE browser__tabs SET created_at_utc = '2099-01-01T00:00:00.000Z', updated_at_utc = '2099-01-01T00:00:00.000Z'").hasValue());
    }

    {
        HeadlessProduct product(data());
        boot(product);
        const auto view = SurfaceReader(product.declared(page)).nodes("webView").front();
        product.emit(page, view, "navigation", {{"url", "https://example.org/later"}, {"title", "Later"}, {"loading", false}, {"canGoBack", true}, {"canGoForward", false}});
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return tabs(product).front()["url"] == "https://example.org/later"; }));
        // clang-format on
        EXPECT_EQ(product.query("SELECT updated_at_utc FROM browser__tabs").front()["updated_at_utc"], "2099-01-01T00:00:00.000Z");
        product.stop();
    }

    HeadlessProduct product(data());
    boot(product);
    EXPECT_EQ(tabs(product).front()["url"], "https://example.org/later");
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// The address the reader is typing stays in the field while another tab loads, since only the active tab or its address changing writes the field again.
TEST_F(BrowserTest, KeepsWhatTheReaderTypesWhileAnotherTabLoads) {
    HeadlessProduct product(data());
    boot(product);
    product.webViews().openers.back()("https://example.org/background", true);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return tabs(product).size() == 2U; }));
    // clang-format on
    const auto address = SurfaceReader(product.declared(page)).nodes("textField").front();
    product.emit(page, address, "change", {{"value", "half typed"}}, {{"value", "value"}});
    product.frame();

    const auto background = SurfaceReader(product.declared(page)).nodes("webView")[1];
    product.emit(page, background, "navigation", {{"url", "https://example.org/background/next"}, {"title", "Next"}, {"loading", false}, {"canGoBack", true}, {"canGoForward", false}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return tabs(product)[1]["url"] == "https://example.org/background/next"; }));
    // clang-format on
    product.settle(std::chrono::milliseconds(100));

    EXPECT_EQ(SurfaceReader(product.declared(page)).properties(address)["value"], "half typed");

    // A background page that only loads again, at the same address and under the same title, changes nothing on screen or in the store.
    const std::size_t changes = product.declared(page)->changes;
    const std::size_t writes = product.calls("workpane_database_transaction") + product.calls("workpane_database_run");
    product.emit(page, background, "navigation", {{"url", "https://example.org/background/next"}, {"title", "Next"}, {"loading", true}, {"canGoBack", true}, {"canGoForward", false}});
    product.settle(std::chrono::milliseconds(100));
    EXPECT_EQ(product.declared(page)->changes, changes);
    EXPECT_EQ(product.calls("workpane_database_transaction") + product.calls("workpane_database_run"), writes);

    // A new address with a new title is kept in one write.
    product.emit(page, background, "navigation", {{"url", "https://example.org/background/last"}, {"title", "Last"}, {"loading", false}, {"canGoBack", true}, {"canGoForward", false}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return tabs(product)[1]["title"] == "Last"; }));
    // clang-format on
    EXPECT_EQ(product.calls("workpane_database_transaction") + product.calls("workpane_database_run"), writes + 1);
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A file a page downloaded is announced with its name once it is saved in the downloads folder, and a download that failed is announced with the reason kept in the log.
TEST_F(BrowserTest, TellsTheReaderWhereADownloadWasSaved) {
    HeadlessProduct product(data());
    boot(product);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.webViews().downloaders.empty(); }));
    // clang-format on
    const std::filesystem::path saved = RootedPath::of("Downloads") / "report.pdf";

    product.webViews().downloaders.back()({saved, true, ""});
    // clang-format off
    EXPECT_TRUE(product.frameUntil([&]() { return product.product().shell().toasts().showing("Download complete", "The file \"report.pdf\" was saved in the downloads folder"); }));
    // clang-format on

    product.webViews().downloaders.back()({saved, false, "The network went away"});
    // clang-format off
    EXPECT_TRUE(product.frameUntil([&]() { return product.product().shell().toasts().showing("Download failed", "The file \"report.pdf\" could not be saved in the downloads folder"); }));
    EXPECT_TRUE(product.frameUntil([&]() { return !product.query("SELECT details_json FROM logs__entries WHERE level = 'warning' AND category = 'download'").empty(); }));
    // clang-format on
    const auto warnings = product.query("SELECT details_json FROM logs__entries WHERE level = 'warning' AND category = 'download'");
    EXPECT_NE(warnings.front()["details_json"].get<std::string>().find("The network went away"), std::string::npos);
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A page asking for the camera and the microphone waits for the reader, whose answer in the dialog reaches the page, allowed or refused.
TEST_F(BrowserTest, AsksTheReaderBeforeAPageUsesTheCamera) {
    HeadlessProduct product(data());
    boot(product);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.webViews().askers.empty(); }));
    // clang-format on

    product.webViews().askers.back()({7, "https://meet.example", true, true});
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.webViews().answers.size() == 1U; }));
    // clang-format on
    EXPECT_EQ(product.webViews().answers[0], std::make_pair(std::uint64_t{7}, true));

    product.webViews().askers.back()({8, "https://voice.example", false, true});
    ASSERT_TRUE(product.answerDialog("cancel"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.webViews().answers.size() == 2U; }));
    // clang-format on
    EXPECT_EQ(product.webViews().answers[1], std::make_pair(std::uint64_t{8}, false));
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(BrowserTest, OpensTheHomepageAndBrowsesWithTheToolbar) {
    HeadlessProduct product(data());
    boot(product);

    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return tabItems(product)[0]["text"] == "Page 1" && tabs(product).size() == 1U && tabs(product)[0]["title"] == "Page 1"; }));
    // clang-format on
    EXPECT_EQ(tabs(product)[0]["url"], "about:blank");
    EXPECT_EQ(tabs(product)[0]["active"], 1);
    EXPECT_EQ(product.webViews().navigations.back(), "about:blank");

    // An address without a scheme receives HTTPS, and the page it reaches is stored for the tab.
    emit(product, page, "textField", 0, "submit", {{"value", "example.com/docs"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return tabs(product)[0]["url"] == "https://example.com/docs"; }));
    // clang-format on
    EXPECT_EQ(product.webViews().navigations.back(), "https://example.com/docs");

    const auto back = SurfaceReader(product.declared(page)).nodeShowing("browser.actions.back");
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(SurfaceReader(product.declared(page)).properties(*back)["enabled"], true);
    product.emit(page, *back, "click", json::object());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return tabs(product)[0]["url"] == "about:blank"; }));
    // clang-format on

    click(product, page, "browser.actions.reload");
    product.press(ImGuiKey_F5);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.webViews().commands.size() == 3U; }));
    // clang-format on
    EXPECT_EQ(product.webViews().commands, (std::vector<std::string>{"back", "reload", "reload"}));

    // A page that keeps loading turns Reload into Stop, and stopping it turns them back.
    product.webViews().holdLoading = true;
    emit(product, page, "textField", 0, "submit", {{"value", "https://slow.example/"}});
    const auto stop = SurfaceReader(product.declared(page)).nodeShowing("browser.actions.stop");
    const auto reload = SurfaceReader(product.declared(page)).nodeShowing("browser.actions.reload");
    ASSERT_TRUE(stop.has_value() && reload.has_value());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(page)).properties(*stop)["visible"] == true; }));
    // clang-format on
    EXPECT_EQ(SurfaceReader(product.declared(page)).properties(*reload)["visible"], false);
    product.emit(page, *stop, "click", json::object());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(page)).properties(*stop)["visible"] == false; }));
    // clang-format on
    EXPECT_EQ(product.webViews().commands.back(), "stop");

    // An address the browser refuses is never sent to the page.
    const auto sent = product.webViews().navigations.size();
    emit(product, page, "textField", 0, "submit", {{"value", "ftp://example.com"}});
    product.settle(std::chrono::milliseconds(100));
    EXPECT_EQ(product.webViews().navigations.size(), sent);

    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A tooltip drawn over the page is named to the native views at the end of the frame, so the web view leaves it uncovered.
TEST_F(BrowserTest, NamesTheWindowsDrawnOverThePage) {
    HeadlessProduct product(data());
    boot(product);
    product.moveTo(ImVec2(30.0F, 125.0F));
    // clang-format off
    const auto overPage = [&]() { return std::ranges::any_of(product.webViews().floating, [](const ImRect& window) { return window.Max.x > 80.0F; }); };
    // clang-format on
    ASSERT_TRUE(product.frameUntil(overPage));
    product.moveTo(ImVec2(600.0F, 600.0F));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.webViews().floating.empty(); }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(BrowserTest, KeepsItsTabsInOrderAcrossARestart) {
    {
        HeadlessProduct product(data());
        boot(product);
        product.press(HeadlessProduct::command() | ImGuiKey_T);
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return tabItems(product).size() == 2U && tabs(product).size() == 2U; }));
        // clang-format on

        // A new tab keeps the page of the first one, so opening it builds exactly one more web view.
        EXPECT_EQ(product.webViews().created, 2);
        const std::string second = tabItems(product)[1]["id"];
        EXPECT_EQ(current(product), second);
        emit(product, page, "textField", 0, "submit", {{"value", "https://second.example/"}});
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return tabs(product)[1]["url"] == "https://second.example/"; }));
        // clang-format on

        // A page asking for a new window opens a third tab on that address.
        product.webViews().openers.back()("https://example.org/", false);
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return tabs(product).size() == 3U && tabs(product)[2]["url"] == "https://example.org/"; }));
        // clang-format on

        emit(product, page, "tabs", 0, "move", {{"id", second}, {"index", 0}});
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return tabs(product)[0]["id"] == second; }));
        // clang-format on

        // Closing the active third tab activates its neighbour, which is the first tab now standing second.
        product.press(HeadlessProduct::command() | ImGuiKey_W);
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return tabs(product).size() == 2U; }));
        // clang-format on
        EXPECT_EQ(tabs(product)[0]["id"], second);
        EXPECT_EQ(tabs(product)[1]["active"], 1);
        product.stop();
        EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    }

    HeadlessProduct product(data());
    boot(product);
    ASSERT_EQ(tabItems(product).size(), 2U);
    EXPECT_EQ(tabItems(product)[0]["tooltip"], "https://second.example/");
    EXPECT_EQ(current(product), tabItems(product)[1]["id"].get<std::string>());

    // The tab keys of the platforms step through the tabs and go round past either end.
    const std::string first = tabItems(product)[0]["id"];
    product.press(HeadlessProduct::command() | ImGuiKey_Tab);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return current(product) == first; }));
    // clang-format on
    product.press(HeadlessProduct::command() | ImGuiKey_PageUp);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return current(product) == tabItems(product)[1]["id"].get<std::string>(); }));
    // clang-format on

    // Closing every tab leaves the empty state, and its button opens the homepage again, with mod and F4 closing a tab, which is control and F4 on Windows.
    product.press(HeadlessProduct::command() | ImGuiKey_F4);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return tabItems(product).size() == 1U; }));
    // clang-format on
    emit(product, page, "tabs", 0, "close", {{"id", tabItems(product)[0]["id"]}});
    product.settle(std::chrono::milliseconds(50));

    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return tabItems(product).empty() && tabs(product).empty(); }));
    // clang-format on
    const auto emptyState = SurfaceReader(product.declared(page)).nodes("emptyState");
    ASSERT_FALSE(emptyState.empty());
    click(product, page, "browser.actions.new-tab");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return tabs(product).size() == 1U && tabs(product)[0]["url"] == "about:blank"; }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(BrowserTest, OrganizesBookmarksInGroupsAndKeepsThem) {
    {
        HeadlessProduct product(data());
        boot(product);
        const std::string first = "dialog:browser:1";
        const std::string second = "dialog:browser:2";

        click(product, page, "browser.bookmarks.add-group");
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(first)).mounted(); }));
        // clang-format on
        emit(product, first, "textField", 0, "change", {{"value", "  Reading  "}});
        product.frame();
        ASSERT_TRUE(product.answerDialog("save"));
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT name FROM browser__bookmark_groups").size() == 1U; }));
        // clang-format on
        EXPECT_EQ(product.query("SELECT name FROM browser__bookmark_groups")[0]["name"], "Reading");

        // A new bookmark starts from the page on screen and lands in the ungrouped collection.
        click(product, page, "browser.bookmarks.add-bookmark");
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(second)).mounted(); }));
        // clang-format on
        EXPECT_TRUE(SurfaceReader(product.declared(second)).showsText("about:blank"));
        emit(product, second, "textField", 0, "change", {{"value", "Docs"}});
        emit(product, second, "textField", 1, "change", {{"value", "docs.example.com"}});
        product.frame();
        ASSERT_TRUE(product.answerDialog("save"));
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT id FROM browser__bookmarks").size() == 1U; }));
        // clang-format on

        // A bookmark whose address the browser refuses is not kept.
        click(product, page, "browser.bookmarks.add-bookmark");
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared("dialog:browser:3")).mounted(); }));
        // clang-format on
        emit(product, "dialog:browser:3", "textField", 1, "change", {{"value", "ftp://files.example.com"}});
        product.frame();
        ASSERT_TRUE(product.answerDialog("save"));
        product.settle(std::chrono::milliseconds(100));
        EXPECT_EQ(product.query("SELECT id FROM browser__bookmarks").size(), 1U);

        const auto stored = product.query("SELECT id, group_id, url FROM browser__bookmarks");
        EXPECT_TRUE(stored[0]["group_id"].is_null());
        EXPECT_EQ(stored[0]["url"], "https://docs.example.com");
        const std::string bookmark = stored[0]["id"];
        const std::string group = product.query("SELECT id FROM browser__bookmark_groups")[0]["id"];

        emit(product, page, "tree", 0, "move", {{"item", bookmark}, {"parent", group}, {"index", 0}});
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT group_id FROM browser__bookmarks")[0]["group_id"] == group; }));
        // clang-format on

        // Opening a bookmark in a new tab reads it there.
        emit(product, page, "tree", 0, "item-menu", {{"id", bookmark}, {"item", "open-new"}});
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return tabs(product).size() == 2U && tabs(product)[1]["url"] == "https://docs.example.com"; }));
        // clang-format on
        product.stop();
        EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    }

    HeadlessProduct product(data());
    boot(product);
    const auto tree = SurfaceReader(product.declared(page)).nodes("tree");
    ASSERT_FALSE(tree.empty());
    const json items = SurfaceReader(product.declared(page)).properties(tree.front())["items"];
    ASSERT_EQ(items.size(), 2U);
    EXPECT_EQ(items[0]["text"]["key"], "browser.bookmarks.ungrouped");
    EXPECT_EQ(items[1]["text"], "Reading");
    EXPECT_EQ(items[1]["children"][0]["text"], "Docs");

    // A group the reader closed stays closed when the panel is drawn again, while the others stay open.
    emit(product, page, "tree", 0, "toggle", {{"id", items[1]["id"]}, {"expanded", false}});
    emit(product, page, "tree", 0, "select", {{"id", items[1]["id"]}});
    product.frame();
    const json redrawn = SurfaceReader(product.declared(page)).properties(tree.front())["items"];
    EXPECT_EQ(redrawn[0]["expanded"], true);
    EXPECT_EQ(redrawn[1]["expanded"], false);

    // Removing the group hands its bookmark to the ungrouped collection.
    click(product, page, "browser.actions.remove");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().shell().dialogs().active(); }));
    // clang-format on
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT id FROM browser__bookmark_groups").empty(); }));
    // clang-format on
    EXPECT_TRUE(product.query("SELECT group_id FROM browser__bookmarks")[0]["group_id"].is_null());
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(BrowserTest, OpensAServedFolderAskedForByTheWebServer) {
    HeadlessProduct product(data());
    boot(product);
    ASSERT_TRUE(product.navigate("web-server:manager").hasValue());
    product.frame();

    // Another plugin reaches the browser only through its capability, which shows the page it asked for in a new tab.
    const auto loaded = product.product().runtime().loadString(R"(
        local events = require("workpane.events")
        local task = require("workpane.task")
        task.run("web-server", "suite", function()
            local _, failure = events.request("web-server", "workspace.page.open", { url = "http://127.0.0.1:8080/" }):await()
            local _, refused = events.request("web-server", "workspace.page.open", { url = "ftp://example.com" }):await()
            local _, malformed = events.request("web-server", "workspace.page.open", { address = "example.com" }):await()
            require("workpane.bridge").call("workpane_log", { plugin = "workpane", level = "info", category = "suite", message = tostring(failure == nil) .. " " .. refused.code .. " " .. malformed.code, details = {} })
        end)
    )",
                                                               "capability");
    ASSERT_TRUE(loaded.hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.query("SELECT message FROM logs__entries WHERE message = 'true browser_address_invalid browser_request_invalid'").empty(); }));
    // clang-format on
    EXPECT_EQ(product.product().shell().destination(), "browser:web");
    EXPECT_EQ(tabs(product).back()["url"], "http://127.0.0.1:8080/");
    EXPECT_EQ(tabs(product).back()["active"], 1);
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(BrowserTest, CommitsAValidHomepageAndRefusesAnInvalidOne) {
    HeadlessProduct product(data());
    boot(product);
    ASSERT_TRUE(product.navigate(ui::Shell::settingsDestination).hasValue());
    product.frame();
    product.press(HeadlessProduct::command() | ImGuiKey_F);
    product.frame();
    product.type("homepage");
    const std::string section = "settings:browser:browser:general";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(section)).mounted(); }));
    // clang-format on

    // An address without a scheme is kept as the reader wrote it, once for Enter and leaving the field together, and opens normalized.
    const std::size_t writes = product.calls("workpane_preferences_write");
    emit(product, section, "textField", 0, "submit", {{"value", "example.com"}});
    emit(product, section, "textField", 0, "blur", {{"value", "example.com"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().preferences().document("browser").value("homepage", "") == "example.com"; }));
    // clang-format on
    product.settle(std::chrono::milliseconds(100));
    EXPECT_EQ(product.calls("workpane_preferences_write") - writes, 1U);

    emit(product, section, "textField", 0, "submit", {{"value", "not an address"}});
    product.settle(std::chrono::milliseconds(100));
    EXPECT_EQ(product.product().preferences().document("browser").value("homepage", ""), "example.com");
    const auto field = SurfaceReader(product.declared(section)).nodes("textField").front();
    EXPECT_EQ(SurfaceReader(product.declared(section)).properties(field)["value"], "example.com");

    // The next tab opens on the committed homepage, once the page has been on screen for the frame that routes its shortcuts.
    ASSERT_TRUE(product.navigate("browser:web").hasValue());
    product.settle(std::chrono::milliseconds(100));
    product.press(HeadlessProduct::command() | ImGuiKey_T);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return tabs(product).size() == 2U && tabs(product)[1]["url"] == "https://example.com"; }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(BrowserTest, RefusesToStartOverASessionWithTwoActiveTabs) {
    {
        HeadlessProduct product(data());
        boot(product);
        product.stop();
    }

    {
        auto database = persistence::Database::open(data() / persistence::DatabaseBootstrap::databaseName);
        ASSERT_TRUE(database.hasValue());
        ASSERT_TRUE(database.value().run("DROP INDEX browser__tabs_active_index").hasValue());
        ASSERT_TRUE(database.value().run("INSERT INTO browser__tabs(id, position, title, url, created_at_utc, updated_at_utc, active) VALUES('second', 1, 'Second', 'about:blank', '2026-01-01T00:00:00.000Z', '2026-01-01T00:00:00.000Z', 1)").hasValue());
    }

    HeadlessProduct product(data());
    ASSERT_TRUE(product.boot().hasValue());
    EXPECT_FALSE(product.navigate("browser:web").hasValue());
    product.stop();

    // The index that keeps one tab active is gone, which the check of the stored tables names before any row is read.
    const auto stored = errors(product);
    ASSERT_EQ(stored.size(), 1U);
    const json details = json::parse(stored[0]["details_json"].get<std::string>());
    EXPECT_EQ(details["code"], "database_schema_mismatch");
    EXPECT_EQ(details["detail"], "browser__tabs_active_index");
}

} // namespace workpane::tests
