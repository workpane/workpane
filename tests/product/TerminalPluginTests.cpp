#include "app/Product.h"
#include "persistence/Database.h"
#include "persistence/DatabaseBootstrap.h"
#include "persistence/PreferenceStore.h"
#include "support/FileAddress.h"
#include "support/HeadlessProduct.h"
#include "support/LocalPort.h"
#include "support/Resources.h"
#include "support/RootedPath.h"
#include "support/SurfaceReader.h"
#include "support/SystemRecord.h"
#include "support/TemporaryDirectory.h"
#include "support/TerminalProcessRecord.h"
#include "support/TerminalRecord.h"
#include "support/WebViewRecord.h"
#include "ui/Fonts.h"
#include "ui/model/RenderContext.h"
#include "ui/shell/Shell.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::tests {

using nlohmann::json;

// Drives the Terminal plugin through its workspace bar, its panes, its shelf and its settings, against recorded shells that report what they were asked.
class TerminalPluginTest : public ::testing::Test {
  protected:
    static constexpr std::string_view view{"view:terminal:workspace"};
    static constexpr std::string_view section{"settings:terminal:terminal:general"};

    [[nodiscard]] const std::filesystem::path& data() const {
        return m_data.path();
    }

    [[nodiscard]] static std::string home() {
        return std::filesystem::temp_directory_path().generic_string();
    }

    // A folder asked to be served is named without the separator a path may end with.
    [[nodiscard]] static std::string folder(std::string path) {
        while (path.size() > 1 && (path.back() == '/' || path.back() == '\\')) {
            path.pop_back();
        }

        return path;
    }

    static void boot(HeadlessProduct& product) {
        ASSERT_TRUE(product.boot().hasValue());
        ASSERT_TRUE(product.navigate("terminal:workspace").hasValue());
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return !product.terminals().processes.empty() && !document(product).is_null(); }));
        // clang-format on
    }

    [[nodiscard]] static json document(const HeadlessProduct& product) {
        const auto rows = product.query("SELECT data_json FROM terminal__state WHERE scope_id = 'workspace'");
        return rows.size() == 1 ? json::parse(rows[0]["data_json"].get<std::string>()) : json();
    }

    [[nodiscard]] static std::vector<ui::NodeId> nodes(HeadlessProduct& product, std::string_view kind, std::string_view surface = view) {
        return SurfaceReader(product.declared(surface)).nodes(kind);
    }

    [[nodiscard]] static json properties(HeadlessProduct& product, ui::NodeId node, std::string_view surface = view) {
        return SurfaceReader(product.declared(surface)).properties(node);
    }

    // The containers that take a dragged terminal are the panes and the empty slots, told apart by the centering only an empty slot asks for.
    [[nodiscard]] static std::vector<ui::NodeId> targets(HeadlessProduct& product, bool empty) {
        std::vector<ui::NodeId> found;

        for (const auto node : nodes(product, "column")) {
            const json props = properties(product, node);

            if (props.contains("accepts") && props.contains("justify") == empty) {
                found.push_back(node);
            }
        }

        return found;
    }

    [[nodiscard]] static std::optional<ui::NodeId> buttonWithText(HeadlessProduct& product, std::string_view literal) {
        for (const auto node : nodes(product, "button")) {
            if (properties(product, node).value("text", json()) == std::string(literal)) {
                return node;
            }
        }

        return std::nullopt;
    }

    static void click(HeadlessProduct& product, std::string_view key, std::string_view surface = view) {
        const auto node = SurfaceReader(product.declared(surface)).nodeShowing(key);
        ASSERT_TRUE(node.has_value()) << key;
        product.emit(surface, *node, "click", json::object());
    }

    static void emit(HeadlessProduct& product, std::string_view kind, std::size_t index, std::string_view name, json value) {
        const auto found = nodes(product, kind);
        ASSERT_GT(found.size(), index) << kind;
        product.emit(view, found[index], name, std::move(value));
    }

    static void drop(HeadlessProduct& product, ui::NodeId target, const json& session) {
        product.emit(view, target, "drop", {{"kind", "terminal.session"}, {"value", session}});
    }

    [[nodiscard]] static bool alive(TerminalProcessRecord& process) {
        const std::lock_guard lock(process.mutex);
        return process.alive;
    }

    // A prompt opens with its text selected, so typing replaces it before the confirming button is pressed.
    static void answerPrompt(HeadlessProduct& product, std::string_view value) {
        product.frame();
        product.press(HeadlessProduct::command() | ImGuiKey_A);
        product.type(value);
        ASSERT_TRUE(product.answerDialog("confirm"));
    }

    [[nodiscard]] static std::size_t linkedTerminals(const HeadlessProduct& product) {
        return product.query("SELECT id FROM web_server__configurations WHERE terminal_id IS NOT NULL").size();
    }

    [[nodiscard]] static std::vector<json> errors(const HeadlessProduct& product) {
        return product.query("SELECT source, category, message, details_json FROM logs__entries WHERE level = 'error'");
    }

  private:
    TemporaryDirectory m_data;
};

TEST_F(TerminalPluginTest, StartsOneTerminalInTheHomeFolderAndRestartsItWhereItStood) {
    TemporaryDirectory moved;

    {
        HeadlessProduct product(data());
        boot(product);
        const json stored = document(product);

        ASSERT_EQ(stored["tabs"].size(), 1U);
        ASSERT_EQ(stored["sessions"].size(), 1U);
        EXPECT_EQ(stored["tabs"][0]["name"], "Workspace 1");
        EXPECT_EQ(stored["tabs"][0]["preset"], "1-single");
        EXPECT_EQ(stored["tabs"][0]["slots"], json::array({stored["sessions"][0]["id"]}));
        EXPECT_EQ(stored["tabs"][0]["focused"], stored["sessions"][0]["id"]);
        EXPECT_EQ(stored["sessions"][0]["name"], "sh");
        EXPECT_EQ(stored["sessions"][0]["shell"], RootedPath::of("bin/sh").generic_string());
        EXPECT_EQ(stored["sessions"][0]["directory"], home());
        EXPECT_EQ(product.terminals().processes.front()->launch.directory, std::filesystem::path(home()));
        EXPECT_EQ(product.terminals().processes.front()->launch.shell, RootedPath::of("bin/sh"));
        EXPECT_EQ(product.terminals().processes.front()->launch.historyFile, data() / "terminal" / "history" / (stored["sessions"][0]["id"].get<std::string>() + ".history"));

        // The shell reports the folder it moved to, which the pane shows and the next start opens.
        emit(product, "terminal", 0, "directory", {{"path", moved.path().generic_string()}});
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return document(product)["sessions"][0]["directory"] == moved.path().generic_string(); }));
        // clang-format on
        EXPECT_TRUE(SurfaceReader(product.declared(view)).showsText(moved.path().generic_string()));
        product.stop();
        EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    }

    // A terminal comes back with the shell it was started with, whatever the default shell became since.
    {
        auto database = persistence::Database::open(data() / persistence::DatabaseBootstrap::databaseName);
        ASSERT_TRUE(database.hasValue());
        ASSERT_TRUE(database.value().run("UPDATE terminal__state SET data_json = json_set(data_json, '$.sessions[0].shell', ?)", {RootedPath::of("bin/zsh").generic_string()}).hasValue());
    }

    HeadlessProduct product(data());
    boot(product);
    ASSERT_EQ(product.terminals().processes.size(), 1U);
    EXPECT_EQ(product.terminals().processes.front()->launch.directory, moved.path());
    EXPECT_EQ(product.terminals().processes.front()->launch.shell, RootedPath::of("bin/zsh"));
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// The shells of the terminals start with the product, before the Terminal destination is ever shown.
// A minimized window draws nothing while its terminals keep reading their shells and their events keep reaching the plugin, so a shell that ends meanwhile is shown as ended once the window is restored.
TEST_F(TerminalPluginTest, KeepsItsTerminalsWorkingWhileTheWindowIsMinimized) {
    HeadlessProduct product(data());
    ASSERT_TRUE(product.boot().hasValue());
    ASSERT_TRUE(product.navigate("terminal:workspace").hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.terminals().processes.empty(); }));
    // clang-format on
    auto& shell = *product.terminals().processes.front();

    {
        const std::lock_guard lock(shell.mutex);
        shell.output += "written while minimized\r\n";
        shell.exit = 3;
    }

    // clang-format off
    ASSERT_TRUE(product.hiddenUntil([&]() { const std::lock_guard lock(shell.mutex); return shell.output.empty() && SurfaceReader(product.declared(view)).showsKey("terminal.session.process-exited"); }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(TerminalPluginTest, StartsItsShellsWithTheProductBeforeItsViewIsShown) {
    HeadlessProduct product(data());
    ASSERT_TRUE(product.boot().hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.terminals().processes.empty(); }));
    // clang-format on
    EXPECT_NE(product.product().shell().destination(), "terminal:workspace");
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(TerminalPluginTest, PlacesNewTerminalsInTheLayoutAndKeepsTheOthersRunningOnTheShelf) {
    HeadlessProduct product(data());
    boot(product);
    const std::string first = document(product)["sessions"][0]["id"];

    // A full layout gives the new terminal the place of the focused one, which moves to the shelf still running.
    click(product, "terminal.actions.new-terminal");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return document(product)["sessions"].size() == 2U && product.terminals().processes.size() == 2U; }));
    // clang-format on
    json stored = document(product);
    const std::string second = stored["sessions"][1]["id"];
    EXPECT_EQ(stored["tabs"][0]["slots"], json::array({second}));
    EXPECT_EQ(stored["tabs"][0]["shelf"], json::array({first}));
    EXPECT_EQ(stored["tabs"][0]["focused"], second);
    EXPECT_TRUE(alive(*product.terminals().processes[0]));

    // Two columns add an empty slot, and a shelved terminal clicked on the shelf comes back into it.
    emit(product, "layoutSwatch", 1, "click", json::object());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return document(product)["tabs"][0]["preset"] == "2-columns"; }));
    // clang-format on
    EXPECT_EQ(document(product)["tabs"][0]["slots"], json::array({second, ""}));
    const auto chip = buttonWithText(product, "sh");
    ASSERT_TRUE(chip.has_value());
    product.emit(view, *chip, "click", json::object());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return document(product)["tabs"][0]["slots"] == json::array({second, first}); }));
    // clang-format on
    EXPECT_TRUE(document(product)["tabs"][0]["shelf"].empty());
    EXPECT_EQ(document(product)["tabs"][0]["focused"], first);

    // A smaller layout moves the terminals of the slots it loses to the shelf instead of closing them.
    emit(product, "layoutSwatch", 0, "click", json::object());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return document(product)["tabs"][0]["preset"] == "1-single"; }));
    // clang-format on
    stored = document(product);
    EXPECT_EQ(stored["tabs"][0]["slots"], json::array({second}));
    EXPECT_EQ(stored["tabs"][0]["shelf"], json::array({first}));
    EXPECT_EQ(stored["tabs"][0]["focused"], second);
    EXPECT_EQ(product.terminals().processes.size(), 2U);
    EXPECT_TRUE(alive(*product.terminals().processes[0]));
    EXPECT_TRUE(alive(*product.terminals().processes[1]));
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(TerminalPluginTest, MovesTerminalsBetweenSlotsAndTheShelfByDragging) {
    HeadlessProduct product(data());
    boot(product);
    const std::string first = document(product)["sessions"][0]["id"];
    click(product, "terminal.actions.new-terminal");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return document(product)["sessions"].size() == 2U; }));
    // clang-format on
    const std::string second = document(product)["sessions"][1]["id"];
    emit(product, "layoutSwatch", 1, "click", json::object());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return document(product)["tabs"][0]["preset"] == "2-columns" && targets(product, true).size() == 1U; }));
    // clang-format on

    // A shelved terminal dropped on the empty slot takes it.
    drop(product, targets(product, true).front(), first);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return document(product)["tabs"][0]["slots"] == json::array({second, first}); }));
    // clang-format on

    // A terminal dropped on another pane trades places with it, where the panes are numbered in the order their terminals were created.
    drop(product, targets(product, false)[1], first);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return document(product)["tabs"][0]["slots"] == json::array({first, second}); }));
    // clang-format on

    // A terminal dropped on the shelf leaves its slot empty.
    const auto shelves = nodes(product, "row");
    // clang-format off
    const auto shelf = std::ranges::find_if(shelves, [&](ui::NodeId node) { return properties(product, node).contains("accepts"); });
    // clang-format on
    ASSERT_NE(shelf, shelves.end());
    drop(product, *shelf, second);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return document(product)["tabs"][0]["shelf"] == json::array({second}); }));
    // clang-format on
    EXPECT_EQ(document(product)["tabs"][0]["slots"], json::array({first, ""}));
    EXPECT_EQ(product.terminals().processes.size(), 2U);
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(TerminalPluginTest, ClosesTerminalsAndTabsOnlyAfterTheReaderConfirms) {
    {
        HeadlessProduct product(data());
        boot(product);
        const std::string firstTab = document(product)["tabs"][0]["id"];

        // A cancelled confirmation keeps the terminal, and a confirmed one ends its shell.
        click(product, "terminal.actions.close-terminal");
        product.frame();
        ASSERT_TRUE(product.answerDialog("cancel"));
        product.frame();
        EXPECT_EQ(document(product)["sessions"].size(), 1U);
        click(product, "terminal.actions.close-terminal");
        product.frame();
        ASSERT_TRUE(product.answerDialog("confirm"));
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return document(product)["sessions"].empty(); }));
        // clang-format on
        EXPECT_FALSE(alive(*product.terminals().processes[0]));
        EXPECT_EQ(document(product)["tabs"][0]["slots"], json::array({""}));
        EXPECT_EQ(document(product)["tabs"][0]["focused"], "");

        // A new tab starts with an empty slot, is renamed with a double click and gets a terminal from its empty slot.
        emit(product, "tabs", 0, "add", json::object());
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return document(product)["tabs"].size() == 2U; }));
        // clang-format on
        const std::string secondTab = document(product)["tabs"][1]["id"];
        EXPECT_EQ(document(product)["selected"], secondTab);
        EXPECT_EQ(document(product)["tabs"][1]["name"], "Workspace 2");
        emit(product, "tabs", 0, "activate", {{"id", secondTab}});
        answerPrompt(product, "Builds");
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return document(product)["tabs"][1]["name"] == "Builds"; }));
        // clang-format on
        product.emit(view, *SurfaceReader(product.declared(view)).nodeShowing("terminal.slot.create"), "click", json::object());
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return document(product)["sessions"].size() == 1U && product.terminals().processes.size() == 2U; }));
        // clang-format on

        // Closing a tab ends every terminal it holds, and closing the last tab leaves the workspace without tabs and starts no shell.
        emit(product, "tabs", 0, "close", {{"id", secondTab}});
        product.frame();
        ASSERT_TRUE(product.answerDialog("confirm"));
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return document(product)["tabs"].size() == 1U; }));
        // clang-format on
        EXPECT_TRUE(document(product)["sessions"].empty());
        EXPECT_FALSE(alive(*product.terminals().processes[1]));
        EXPECT_EQ(document(product)["selected"], firstTab);

        emit(product, "tabs", 0, "close", {{"id", firstTab}});
        product.frame();
        ASSERT_TRUE(product.answerDialog("confirm"));
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return document(product)["tabs"].empty(); }));
        // clang-format on
        EXPECT_EQ(document(product)["selected"], "");
        EXPECT_TRUE(document(product)["sessions"].empty());
        EXPECT_EQ(product.terminals().processes.size(), 2U);
        EXPECT_TRUE(SurfaceReader(product.declared(view)).nodeShowing("terminal.workspace.empty").has_value());
        EXPECT_TRUE(nodes(product, "tabs").empty() || properties(product, nodes(product, "tabs")[0])["items"].empty());
        product.stop();
        EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    }

    // The workspace stays without tabs after a restart, and its empty view opens a tab with one terminal.
    HeadlessProduct restarted(data());
    ASSERT_TRUE(restarted.boot().hasValue());
    ASSERT_TRUE(restarted.navigate("terminal:workspace").hasValue());
    // clang-format off
    ASSERT_TRUE(restarted.frameUntil([&]() { return SurfaceReader(restarted.declared(view)).nodeShowing("terminal.workspace.empty").has_value(); }));
    // clang-format on
    EXPECT_TRUE(document(restarted)["tabs"].empty());
    EXPECT_TRUE(restarted.terminals().processes.empty());
    click(restarted, "terminal.workspace.add");
    // clang-format off
    ASSERT_TRUE(restarted.frameUntil([&]() { return document(restarted)["tabs"].size() == 1U && restarted.terminals().processes.size() == 1U; }));
    // clang-format on
    EXPECT_EQ(document(restarted)["tabs"][0]["name"], "Workspace 1");
    EXPECT_EQ(document(restarted)["selected"], document(restarted)["tabs"][0]["id"]);
    EXPECT_EQ(document(restarted)["sessions"].size(), 1U);
    restarted.stop();
    EXPECT_TRUE(errors(restarted).empty()) << json(errors(restarted)).dump();
}

TEST_F(TerminalPluginTest, RenamesRestartsAndFocusesATerminalFromItsHeader) {
    HeadlessProduct product(data());
    boot(product);

    // clang-format off
    const auto shellShown = [&]() { return std::ranges::any_of(nodes(product, "label"), [&](ui::NodeId node) { const json props = properties(product, node); return props.value("text", json()) == "sh" && props.value("visible", true); }); };
    // clang-format on
    EXPECT_FALSE(shellShown());
    emit(product, "menuButton", 0, "select", {{"item", "rename"}});
    answerPrompt(product, "Server logs");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return document(product)["sessions"][0]["name"] == "Server logs"; }));
    // clang-format on

    // A renamed terminal shows its shell beside its name, a detail the header gives up first when it narrows.
    ASSERT_TRUE(product.frameUntil(shellShown));

    // A program that ends shows its code under the terminal, and restarting starts a new shell in the same pane.
    {
        const std::lock_guard lock(product.terminals().processes[0]->mutex);
        product.terminals().processes[0]->exit = 3;
    }

    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(view)).showsKey("terminal.session.process-exited"); }));
    // clang-format on
    click(product, "terminal.actions.restart");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.terminals().processes.size() == 2U; }));
    // clang-format on

    // A shell that cannot start is told under the terminal with Restart beside it, which starts it again once it can.
    product.terminals().refusal = "terminal_shell_not_executable";
    emit(product, "menuButton", 0, "select", {{"item", "restart"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(view)).showsKey("terminal.error.shell-not-executable"); }));
    // clang-format on
    product.terminals().refusal.clear();
    click(product, "terminal.actions.restart");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.terminals().processes.size() == 3U; }));
    // clang-format on

    // Focus mode gives the whole board to one terminal and turns its button into Restore.
    click(product, "terminal.actions.focus");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(view)).showsKey("terminal.actions.restore-layout"); }));
    // clang-format on
    click(product, "terminal.actions.restore-layout");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(view)).showsKey("terminal.actions.focus"); }));
    // clang-format on
    product.stop();
    const auto stored = errors(product);
    ASSERT_EQ(stored.size(), 1U);
    EXPECT_NE(stored[0]["details_json"].get<std::string>().find("terminal_shell_not_executable"), std::string::npos);
}

TEST_F(TerminalPluginTest, AppliesItsSettingsToEveryTerminalAndOpensLinksWhereTheReaderChose) {
    HeadlessProduct product(data());
    boot(product);
    click(product, "terminal.actions.new-terminal");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return nodes(product, "terminal").size() == 2U; }));
    // clang-format on

    // The terminal that had the keyboard lets it go one frame after it leaves the screen, and the search shortcut is attributed the frame after that.
    ASSERT_TRUE(product.navigate(ui::Shell::settingsDestination).hasValue());
    product.frame();
    product.frame();
    product.press(HeadlessProduct::command() | ImGuiKey_F);
    product.frame();
    product.type("intensity");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(section)).mounted(); }));
    // clang-format on

    // A setting reaches the terminal on the shelf as well as the one on screen, once it is stored.
    product.emit(section, nodes(product, "combo", section)[1], "change", {{"value", "vivid"}});
    product.emit(section, nodes(product, "toggle", section)[1], "change", {{"checked", true}});
    // clang-format off
    const auto applied = [&]() { return std::ranges::all_of(nodes(product, "terminal"), [&](ui::NodeId terminal) { return properties(product, terminal)["palette"] == "vivid" && properties(product, terminal)["clipboardWrites"] == true; }); };
    // clang-format on
    ASSERT_TRUE(product.frameUntil(applied));
    EXPECT_EQ(product.product().preferences().document("terminal").value("palette", ""), "vivid");
    EXPECT_TRUE(product.product().preferences().document("terminal").value("clipboardWrites", false));

    // The zoom keys of one terminal change the size of every terminal and the value the settings show.
    emit(product, "terminal", 0, "zoom", {{"fontSize", 12.0}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().preferences().document("terminal").value("fontSize", 0) == 12; }));
    ASSERT_TRUE(product.frameUntil([&]() { return properties(product, nodes(product, "terminal")[1])["fontSize"] == 12; }));
    // clang-format on
    EXPECT_EQ(properties(product, nodes(product, "numberField", section).front(), section)["value"], 12);

    // A link opens in the Browser plugin, or in the system browser once the reader asks for it.
    emit(product, "terminal", 0, "link", {{"url", "https://example.com/terminal"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return std::ranges::find(product.webViews().navigations, "https://example.com/terminal") != product.webViews().navigations.end(); }));
    // clang-format on
    EXPECT_TRUE(product.system().openedUrls.empty());

    product.emit(section, nodes(product, "toggle", section)[2], "change", {{"checked", true}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().preferences().document("terminal").value("openLinksExternally", false); }));
    // clang-format on
    emit(product, "terminal", 0, "link", {{"url", "https://example.com/system"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.system().openedUrls.empty(); }));
    // clang-format on
    EXPECT_EQ(product.system().openedUrls.back(), "https://example.com/system");

    // A file address shows its file in the file manager of the system instead of leaving for a browser.
    const std::filesystem::path report = RootedPath::of("tmp/work notes/report.txt");
    emit(product, "terminal", 0, "link", {{"url", FileAddress::of(report)}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.system().revealedPaths.empty(); }));
    // clang-format on
    EXPECT_EQ(product.system().revealedPaths.back(), report);
    EXPECT_EQ(product.system().openedUrls.size(), 1U);

    // A notification a program sends is shown under its own title or the name of its terminal, and input the shell did not take is reported.
    emit(product, "terminal", 0, "notification", {{"title", ""}, {"body", "Build finished"}});
    emit(product, "terminal", 0, "input-refused", json::object());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().shell().toasts().showing("sh", "Build finished") && product.product().shell().toasts().showing("Terminal", "The shell has not read what was already sent to it, so the last input was not delivered"); }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// The monospaced families of the machine join the choices of the settings, and the one the reader chooses is stored and written by every terminal.
// The cursor of every terminal stays steady until the reader turns blinking on and then blinks at the speed the reader chose, which is chosen only while it blinks.
TEST_F(TerminalPluginTest, BlinksTheCursorOfEveryTerminalOnlyOnceTheReaderAsks) {
    HeadlessProduct product(data());
    boot(product);
    click(product, "terminal.actions.new-terminal");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return nodes(product, "terminal").size() == 2U; }));
    const auto blinks = [&](int interval) { return std::ranges::all_of(nodes(product, "terminal"), [&](ui::NodeId terminal) { return properties(product, terminal)["cursorBlink"] == interval; }); };
    // clang-format on
    EXPECT_TRUE(blinks(0));

    ASSERT_TRUE(product.navigate(ui::Shell::settingsDestination).hasValue());
    product.frame();
    product.frame();
    product.press(HeadlessProduct::command() | ImGuiKey_F);
    product.frame();
    product.type("blink");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(section)).mounted(); }));
    const auto speed = [&]() { return properties(product, nodes(product, "combo", section)[2], section); };
    // clang-format on
    EXPECT_EQ(speed()["value"], "normal");
    EXPECT_EQ(speed()["enabled"], false);

    product.emit(section, nodes(product, "toggle", section).front(), "change", {{"checked", true}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return blinks(530) && speed()["enabled"] == true; }));
    // clang-format on

    product.emit(section, nodes(product, "combo", section)[2], "change", {{"value", "fast"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return blinks(300); }));
    // clang-format on
    EXPECT_EQ(product.product().preferences().document("terminal").value("blinkSpeed", ""), "fast");

    product.emit(section, nodes(product, "toggle", section).front(), "change", {{"checked", false}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return blinks(0) && speed()["enabled"] == false; }));
    // clang-format on
    EXPECT_FALSE(product.product().preferences().document("terminal").value("cursorBlink", true));
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(TerminalPluginTest, WritesInTheFamilyTheReaderChoseAmongTheOnesOfTheMachine) {
    HeadlessProduct product(data());
    product.system().fonts = {{"Inter", Resources::fonts() / "Inter-Regular.ttf"}, {"Missing Mono", data() / "missing.ttf"}};
    boot(product);
    ASSERT_TRUE(product.navigate(ui::Shell::settingsDestination).hasValue());
    product.frame();
    product.frame();
    product.press(HeadlessProduct::command() | ImGuiKey_F);
    product.frame();
    product.type("intensity");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(section)).mounted(); }));
    const auto family = [&]() { return properties(product, nodes(product, "combo", section).front(), section); };
    ASSERT_TRUE(product.frameUntil([&]() { return family()["options"].size() == 3U; }));
    // clang-format on
    EXPECT_EQ(family()["value"], "JetBrains Mono");
    EXPECT_EQ(family()["options"][1]["value"], "Inter");
    EXPECT_EQ(family()["options"][2]["value"], "Missing Mono");

    product.emit(section, nodes(product, "combo", section).front(), "change", {{"value", "Inter"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().preferences().document("terminal").value("fontFamily", "") == "Inter"; }));
    // clang-format on

    // The family is read once the terminal draws it again, and a family whose file is gone leaves the terminal in the bundled face with a warning.
    ASSERT_TRUE(product.navigate("terminal:workspace").hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return properties(product, nodes(product, "terminal").front())["fontFamily"] == "Inter" && product.product().render().fonts().has("Inter"); }));
    // clang-format on
    ASSERT_TRUE(product.navigate(ui::Shell::settingsDestination).hasValue());
    product.emit(section, nodes(product, "combo", section).front(), "change", {{"value", "Missing Mono"}});
    ASSERT_TRUE(product.navigate("terminal:workspace").hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.query("SELECT message FROM logs__entries WHERE level = 'warning' AND category = 'fonts'").empty(); }));
    // clang-format on
    EXPECT_FALSE(product.product().render().fonts().has("Missing Mono"));
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(TerminalPluginTest, AnswersItsShortcutsOnlyWhileItsViewIsOnScreen) {
    HeadlessProduct product(data());
    boot(product);
    const bool mac = ImGui::GetIO().ConfigMacOSXBehaviors;
    const ImGuiKeyChord extra = mac ? ImGuiKeyChord{0} : ImGuiKeyChord{ImGuiMod_Shift};

    // The shortcuts are attributed once the view has been on screen for a frame.
    product.frame();
    product.press(HeadlessProduct::command() | extra | ImGuiKey_N);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return document(product)["sessions"].size() == 2U; }));
    // clang-format on
    product.press(HeadlessProduct::command() | ImGuiMod_Shift | ImGuiKey_T);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return document(product)["tabs"].size() == 2U; }));
    // clang-format on

    ASSERT_TRUE(product.navigate("browser:web").hasValue());
    product.frame();
    product.frame();
    product.press(HeadlessProduct::command() | ImGuiMod_Shift | ImGuiKey_T);
    product.settle(std::chrono::milliseconds(100));
    EXPECT_EQ(document(product)["tabs"].size(), 2U);
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(TerminalPluginTest, StartsATerminalWhoseFolderIsGoneInTheHomeFolder) {
    {
        HeadlessProduct product(data());
        boot(product);
        product.stop();
    }

    {
        auto database = persistence::Database::open(data() / persistence::DatabaseBootstrap::databaseName);
        ASSERT_TRUE(database.hasValue());
        ASSERT_TRUE(database.value().run("UPDATE terminal__state SET data_json = json_set(data_json, '$.sessions[0].directory', ?)", {RootedPath::of("workpane/missing/folder").generic_string()}).hasValue());
    }

    HeadlessProduct product(data());
    boot(product);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return document(product)["sessions"][0]["directory"] == home(); }));
    // clang-format on
    ASSERT_EQ(product.terminals().processes.size(), 1U);
    EXPECT_EQ(product.terminals().processes.front()->launch.directory, std::filesystem::path(home()));
    product.stop();

    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    const auto warnings = product.query("SELECT details_json FROM logs__entries WHERE level = 'warning' AND source = 'terminal'");
    ASSERT_EQ(warnings.size(), 1U);
    EXPECT_NE(warnings[0]["details_json"].get<std::string>().find("terminal_directory_missing"), std::string::npos);
}

TEST_F(TerminalPluginTest, RefusesToStartOverAStoredWorkspaceThatBreaksTheRules) {
    {
        HeadlessProduct product(data());
        boot(product);
        product.stop();
    }

    {
        auto database = persistence::Database::open(data() / persistence::DatabaseBootstrap::databaseName);
        ASSERT_TRUE(database.hasValue());
        ASSERT_TRUE(database.value().run("UPDATE terminal__state SET data_json = json_set(data_json, '$.tabs[0].shelf', json_array(json_extract(data_json, '$.sessions[0].id')))").hasValue());
    }

    HeadlessProduct product(data());
    ASSERT_TRUE(product.boot().hasValue());
    EXPECT_FALSE(product.navigate("terminal:workspace").hasValue());
    product.stop();

    const auto stored = errors(product);
    ASSERT_EQ(stored.size(), 1U);
    EXPECT_NE(stored[0]["details_json"].get<std::string>().find("database_rows_invalid"), std::string::npos);
}

// A change of the terminals leaves the page of the Web Server as it was, since that page shows nothing a terminal changes.
TEST_F(TerminalPluginTest, LeavesTheWebServerPageAsItWasWhenTheTerminalsChange) {
    TemporaryDirectory moved;
    HeadlessProduct product(data());
    boot(product);
    const std::string manager = "view:web-server:manager";
    ASSERT_TRUE(product.navigate("web-server:manager").hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(manager)).mounted(); }));
    // clang-format on
    product.settle(std::chrono::milliseconds(200));
    const std::size_t before = product.declared(manager)->changes;

    emit(product, "terminal", 0, "directory", {{"path", moved.path().generic_string()}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return document(product)["sessions"][0]["directory"] == moved.path().generic_string(); }));
    // clang-format on
    product.settle(std::chrono::milliseconds(200));
    EXPECT_EQ(product.declared(manager)->changes, before);
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A render that changes nothing on screen, such as the focus of the terminal that already had it, sends nothing to the host.
TEST_F(TerminalPluginTest, SendsOnlyWhatChangedOnScreen) {
    HeadlessProduct product(data());
    boot(product);
    product.settle(std::chrono::milliseconds(200));
    const std::size_t before = product.declared(view)->changes;

    emit(product, "terminal", 0, "focus", {{"focused", true}});
    product.settle(std::chrono::milliseconds(200));
    EXPECT_EQ(product.declared(view)->changes, before);
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// Zooming a terminal gives it the focus of the workspace, and a terminal created while one is zoomed ends the zoom and takes the focus on screen.
TEST_F(TerminalPluginTest, ZoomsTheFocusedTerminalAndLeavesTheZoomForANewOne) {
    HeadlessProduct product(data());
    boot(product);
    emit(product, "layoutSwatch", 5, "click", json::object());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return document(product)["tabs"][0]["preset"] == "4-grid"; }));
    // clang-format on
    click(product, "terminal.actions.new-terminal");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return document(product)["sessions"].size() == 2U && product.terminals().processes.size() == 2U; }));
    // clang-format on
    const std::string first = document(product)["sessions"][0]["id"];
    const std::string second = document(product)["sessions"][1]["id"];
    EXPECT_EQ(document(product)["tabs"][0]["focused"], second);

    // The focus button of the first pane is the first one built.
    click(product, "terminal.actions.focus");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return document(product)["tabs"][0]["focused"] == first && SurfaceReader(product.declared(view)).showsKey("terminal.actions.restore-layout"); }));
    // clang-format on

    click(product, "terminal.actions.new-terminal");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return document(product)["sessions"].size() == 3U && !SurfaceReader(product.declared(view)).showsKey("terminal.actions.restore-layout"); }));
    // clang-format on
    EXPECT_EQ(document(product)["tabs"][0]["focused"], document(product)["sessions"][2]["id"]);
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// The history of a terminal the reader closed is removed the next time the workspace loads, while the history of a terminal it keeps stays.
TEST_F(TerminalPluginTest, RemovesTheHistoriesOfTheTerminalsTheReaderClosed) {
    const std::filesystem::path histories = data() / "terminal" / "history";
    std::string kept;
    std::string closed;

    {
        HeadlessProduct product(data());
        boot(product);
        emit(product, "layoutSwatch", 1, "click", json::object());
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return document(product)["tabs"][0]["preset"] == "2-columns"; }));
        // clang-format on
        click(product, "terminal.actions.new-terminal");
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return document(product)["sessions"].size() == 2U; }));
        // clang-format on
        closed = document(product)["sessions"][0]["id"];
        kept = document(product)["sessions"][1]["id"];
        std::filesystem::create_directories(histories);

        for (const std::string& id : {closed, kept}) {
            std::ofstream(histories / (id + ".history")) << "echo one\n";
        }

        click(product, "terminal.actions.close-terminal");
        product.frame();
        ASSERT_TRUE(product.answerDialog("confirm"));
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return document(product)["sessions"].size() == 1U; }));
        // clang-format on
        product.stop();
    }

    HeadlessProduct product(data());
    boot(product);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !std::filesystem::exists(histories / (closed + ".history")); }));
    // clang-format on
    EXPECT_TRUE(std::filesystem::exists(histories / (kept + ".history")));
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A form opened from a terminal that closes while it stays open starts a server of its own, since a link to a closed terminal is no link.
TEST_F(TerminalPluginTest, StartsAServerWhoseTerminalClosedWhileItsFormWasOpen) {
    HeadlessProduct product(data());
    boot(product);
    emit(product, "menuButton", 0, "select", {{"item", "server"}});
    const std::string form = "dialog:web-server:1";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(form)).mounted(); }));
    // clang-format on
    product.emit(form, nodes(product, "numberField", form).front(), "change", {{"value", static_cast<double>(LocalPort::free())}});
    product.frame();

    click(product, "terminal.actions.close-terminal");
    product.frame();
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return document(product)["sessions"].empty(); }));
    // clang-format on
    ASSERT_TRUE(product.answerDialog("start"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !SurfaceReader(product.declared(form)).mounted(); }));
    // clang-format on
    EXPECT_EQ(product.query("SELECT id FROM web_server__configurations").size(), 1U);
    EXPECT_EQ(linkedTerminals(product), 0U);
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// The Web Server follows the terminals through the snapshot, so a server made from the active terminal serves the folder it stands in.
TEST_F(TerminalPluginTest, LetsTheWebServerServeTheFolderOfTheActiveTerminal) {
    HeadlessProduct product(data());
    boot(product);
    emit(product, "menuButton", 0, "select", {{"item", "server"}});
    const std::string form = "dialog:web-server:1";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(form)).mounted(); }));
    // clang-format on
    const auto fields = nodes(product, "textField", form);
    ASSERT_EQ(fields.size(), 3U);
    EXPECT_EQ(properties(product, fields[1], form)["value"], std::filesystem::canonical(folder(home())).generic_string());
    ASSERT_TRUE(product.answerDialog("close"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !SurfaceReader(product.declared(form)).mounted(); }));
    // clang-format on

    ASSERT_TRUE(product.navigate("web-server:manager").hasValue());
    const std::string manager = "view:web-server:manager";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(manager)).showsKey("web-server.manager.from-terminal"); }));
    // clang-format on
    click(product, "web-server.manager.from-terminal", manager);
    const std::string fromTerminal = "dialog:web-server:2";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(fromTerminal)).mounted(); }));
    // clang-format on
    EXPECT_EQ(properties(product, nodes(product, "textField", fromTerminal)[0], fromTerminal)["value"], "sh");
    EXPECT_EQ(properties(product, nodes(product, "textField", fromTerminal)[1], fromTerminal)["value"], home());

    // A server started from the terminal is linked to it, and closing the terminal drops the link while the server keeps its configuration.
    product.emit(fromTerminal, nodes(product, "numberField", fromTerminal).front(), "change", {{"value", static_cast<double>(LocalPort::free())}});
    product.frame();
    ASSERT_TRUE(product.answerDialog("start"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !SurfaceReader(product.declared(fromTerminal)).mounted() && linkedTerminals(product) == 1U; }));
    // clang-format on

    // Serving the folder of the terminal again opens the server it already has, although the terminal names it through a link.
    ASSERT_TRUE(product.navigate("terminal:workspace").hasValue());
    product.frame();
    emit(product, "menuButton", 0, "select", {{"item", "server"}});
    const std::string again = "dialog:web-server:3";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(again)).mounted(); }));
    // clang-format on
    EXPECT_EQ(properties(product, nodes(product, "textField", again)[0], again)["value"], "sh");
    ASSERT_TRUE(product.answerDialog("close"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !SurfaceReader(product.declared(again)).mounted(); }));
    // clang-format on
    EXPECT_EQ(product.query("SELECT id FROM web_server__configurations").size(), 1U);
    ASSERT_TRUE(product.navigate("terminal:workspace").hasValue());
    product.frame();
    click(product, "terminal.actions.close-terminal");
    product.frame();
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return linkedTerminals(product) == 0U; }));
    // clang-format on
    EXPECT_EQ(product.query("SELECT id FROM web_server__configurations").size(), 1U);
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

} // namespace workpane::tests
