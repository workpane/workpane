#include "app/Product.h"
#include "persistence/Database.h"
#include "persistence/DatabaseBootstrap.h"
#include "persistence/PluginDatabase.h"
#include "persistence/PreferenceStore.h"
#include "process/ProcessStream.h"
#include "scripting/ScriptRuntime.h"
#include "support/FileAddress.h"
#include "support/HeadlessProduct.h"
#include "support/Resources.h"
#include "support/RootedPath.h"
#include "support/ScriptHarness.h"
#include "support/SurfaceReader.h"
#include "support/SystemRecord.h"
#include "support/TemporaryDirectory.h"
#include "ui/shell/Shell.h"
#include "ui/shell/ToastOverlay.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace workpane::tests {

using nlohmann::json;

// Drives the Code Editor plugin through its folders, documents, panels and settings, with a language server played by the test.
class CodeEditorPluginTest : public ::testing::Test {
  protected:
    static constexpr std::string_view view{"view:code-editor:editor"};

    void SetUp() override {
        m_root = std::filesystem::canonical(m_folder.path());
    }

    [[nodiscard]] const std::filesystem::path& data() const {
        return m_data.path();
    }

    [[nodiscard]] const std::filesystem::path& root() const {
        return m_root;
    }

    [[nodiscard]] std::string path(std::string_view relative) const {
        return (m_root / relative).generic_string();
    }

    void write(std::string_view relative, std::string_view bytes) const {
        std::filesystem::create_directories((m_root / relative).parent_path());
        std::ofstream(m_root / relative, std::ios::binary) << bytes;
    }

    [[nodiscard]] std::string read(std::string_view relative) const {
        std::ifstream file(m_root / relative, std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    static void boot(HeadlessProduct& product) {
        ASSERT_TRUE(product.boot().hasValue());
        ASSERT_TRUE(product.navigate("code-editor:editor").hasValue());
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(view)).mounted(); }));
        // clang-format on
    }

    [[nodiscard]] static std::vector<ui::NodeId> nodes(HeadlessProduct& product, std::string_view kind, std::string_view surface = view) {
        return SurfaceReader(product.declared(surface)).nodes(kind);
    }

    [[nodiscard]] static json properties(HeadlessProduct& product, ui::NodeId node, std::string_view surface = view) {
        return SurfaceReader(product.declared(surface)).properties(node);
    }

    static void click(HeadlessProduct& product, std::string_view key, std::string_view surface = view) {
        const auto node = SurfaceReader(product.declared(surface)).nodeShowing(key);
        ASSERT_TRUE(node.has_value()) << key;
        product.emit(surface, *node, "click", json::object());
    }

    // The file tree is the first tree of the view, and the outline beside it the second.
    [[nodiscard]] static std::optional<ui::NodeId> tree(HeadlessProduct& product, std::size_t index = 0) {
        const auto found = nodes(product, "tree");
        return found.size() > index ? std::optional<ui::NodeId>(found[index]) : std::nullopt;
    }

    [[nodiscard]] static std::vector<std::string> itemTexts(const json& items) {
        std::vector<std::string> texts;

        for (const auto& item : items) {
            texts.push_back(item.value("text", ""));
        }

        return texts;
    }

    [[nodiscard]] static std::optional<ui::NodeId> editor(HeadlessProduct& product, std::string_view text) {
        for (const auto node : nodes(product, "codeEditor")) {
            if (properties(product, node).value("value", "") == text) {
                return node;
            }
        }

        return std::nullopt;
    }

    // The tabs of the documents are the strip whose items are paths inside the folder.
    [[nodiscard]] std::optional<ui::NodeId> documentTabs(HeadlessProduct& product) const {
        for (const auto node : nodes(product, "tabs")) {
            const json items = properties(product, node).value("items", json::array());

            if (!items.empty() && items[0].value("id", "").starts_with(m_root.generic_string() + "/")) {
                return node;
            }
        }

        return std::nullopt;
    }

    // The bottom panel is the strip that always carries the Search tab.
    [[nodiscard]] static std::vector<std::string> panelTabs(HeadlessProduct& product) {
        for (const auto node : nodes(product, "tabs")) {
            const json items = properties(product, node).value("items", json::array());
            std::vector<std::string> ids;

            for (const auto& item : items) {
                ids.push_back(item["id"]);
            }

            if (!ids.empty() && ids.back() == "search") {
                return ids;
            }
        }

        return {};
    }

    [[nodiscard]] std::vector<std::string> documentTitles(HeadlessProduct& product) const {
        const auto tabs = documentTabs(product);
        return tabs.has_value() ? itemTexts(properties(product, *tabs)["items"]) : std::vector<std::string>{};
    }

    void openFolder(HeadlessProduct& product) const {
        product.dialogs().paths = {m_root.generic_string()};
        click(product, "code-editor.actions.open-folder");
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { const auto found = tree(product); return found.has_value() && !properties(product, *found)["items"].empty(); }));
        // clang-format on
    }

    void openFile(HeadlessProduct& product, std::string_view relative) const {
        const auto found = tree(product);
        ASSERT_TRUE(found.has_value());
        product.emit(view, *found, "activate", {{"id", path(relative)}});
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { const auto tabs = documentTabs(product); return tabs.has_value() && properties(product, *tabs)["current"] == path(relative); }));
        // clang-format on
    }

    [[nodiscard]] static std::vector<json> errors(const HeadlessProduct& product) {
        return product.query("SELECT source, category, message, details_json FROM logs__entries WHERE level = 'error'");
    }

    [[nodiscard]] static std::string framed(const json& message) {
        const std::string body = message.dump();
        return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    }

    // The server reads every framed message the plugin writes and answers a request with the result the test prepared for its method.
    void serve(HeadlessProduct& product) {
        // clang-format off
        product.processes().responder = [this, &product](std::size_t program, std::string_view written) {
            std::string& buffer = m_buffers[program];
            buffer += written;

            for (auto header = buffer.find("\r\n\r\n"); header != std::string::npos; header = buffer.find("\r\n\r\n")) {
                const std::size_t length = std::stoul(buffer.substr(std::string_view("Content-Length: ").size(), header));

                if (buffer.size() < header + 4 + length) {
                    return;
                }

                const json message = json::parse(buffer.substr(header + 4, length));
                buffer.erase(0, header + 4 + length);
                m_received.push_back(message);

                if (message.contains("id") && message.contains("method")) {
                    const auto answer = m_answers.find(message["method"].get<std::string>());
                    product.processes().events[program].output(process::ProcessStream::Output, framed({{"jsonrpc", "2.0"}, {"id", message["id"]}, {"result", answer != m_answers.end() ? answer->second : json()}}));
                }
            }
        };
        // clang-format on
    }

    void answer(std::string method, json result) {
        m_answers[std::move(method)] = std::move(result);
    }

    // Answers the methods of every message the server read, in the order it read them.
    [[nodiscard]] std::vector<std::string> methods() const {
        std::vector<std::string> found;

        for (const auto& message : m_received) {
            found.push_back(message.value("method", ""));
        }

        return found;
    }

    [[nodiscard]] std::vector<json> received(std::string_view method) const {
        std::vector<json> found;

        for (const auto& message : m_received) {
            if (message.value("method", "") == method) {
                found.push_back(message);
            }
        }

        return found;
    }

    // A prompt opens with its text selected, so typing replaces it before the confirming button is pressed.
    static void answerPrompt(HeadlessProduct& product, std::string_view value) {
        // clang-format off
        ASSERT_TRUE(product.frameUntil([]() { return ImGui::GetTopMostPopupModal() != nullptr; }));
        // clang-format on
        product.frame();
        product.press(HeadlessProduct::command() | ImGuiKey_A);
        product.type(value);
        ASSERT_TRUE(product.answerDialog("confirm"));
    }

    [[nodiscard]] static std::optional<ui::NodeId> withText(HeadlessProduct& product, std::string_view kind, std::string_view literal) {
        for (const auto node : nodes(product, kind)) {
            if (properties(product, node).value("text", json()) == std::string(literal)) {
                return node;
            }
        }

        return std::nullopt;
    }

    [[nodiscard]] static std::size_t toasts(HeadlessProduct& product) {
        return product.product().shell().toasts().liveCount();
    }

    [[nodiscard]] static std::vector<json> storedDocuments(const HeadlessProduct& product) {
        return product.query("SELECT path, cursor_line, cursor_column, active FROM code_editor__documents ORDER BY position");
    }

    [[nodiscard]] std::string uri(std::string_view relative) const {
        return FileAddress::of(path(relative));
    }

    static void notify(HeadlessProduct& product, std::size_t program, std::string_view method, json params) {
        product.processes().events[program].output(process::ProcessStream::Output, framed({{"jsonrpc", "2.0"}, {"method", method}, {"params", std::move(params)}}));
    }

  private:
    TemporaryDirectory m_data;
    TemporaryDirectory m_folder;
    std::filesystem::path m_root;
    std::map<std::size_t, std::string> m_buffers;
    std::vector<json> m_received;
    std::map<std::string, json> m_answers;
};

// A folder opened at a root keeps the separator of that root, so joining a name or asking what lies inside it never doubles a separator, on a drive and on a network share as much as at the root of a POSIX system.
TEST(CodeEditorPaths, JoinsAndComparesThePathsOfARoot) {
    tests::ScriptHarness harness;
    const std::string file = (tests::Resources::staged() / "plugins" / "code-editor" / "paths.lua").generic_string();
    ASSERT_TRUE(harness
                    .run("local paths = assert(loadfile(" + nlohmann::json(file).dump() + R"(, "t"))()
        host.test_result({
            joined = { paths.join("/", "etc"), paths.join("C:/", "Users"), paths.join("//server/share", "docs"), paths.join("/home/reader", "notes") },
            inside = { paths.inside("/", "/etc/hosts"), paths.inside("C:/", "C:/Users"), paths.inside("/home/reader", "/home/readers"), paths.inside("/home/reader", "/home/reader") },
            parents = { paths.parent("/etc"), paths.parent("C:/Users"), paths.parent("//server/share/docs"), paths.parent("/") == nil },
            relative = { paths.relative("/", "/etc/hosts"), paths.relative("C:/Users", "C:/Users/reader/a.txt") },
        })
    )")
                    .hasValue());

    ASSERT_EQ(harness.results.size(), 1U);
    const nlohmann::json& result = harness.results[0];
    EXPECT_EQ(result["joined"], nlohmann::json::array({"/etc", "C:/Users", "//server/share/docs", "/home/reader/notes"}));
    EXPECT_EQ(result["inside"], nlohmann::json::array({true, true, false, true}));
    EXPECT_EQ(result["parents"], nlohmann::json::array({"/", "C:/", "//server/share", true}));
    EXPECT_EQ(result["relative"], nlohmann::json::array({"etc/hosts", "reader/a.txt"}));
}

TEST_F(CodeEditorPluginTest, OpensAFolderAndEditsSavesAndClosesItsDocuments) {
    write("src/main.lua", "print('main')\n");
    write("README.md", "# Workpane\n");
    write(".hidden", "secret");
    HeadlessProduct product(data());
    boot(product);
    EXPECT_TRUE(SurfaceReader(product.declared(view)).showsKey("code-editor.view.empty"));

    // The tree lists folders first and leaves hidden entries out.
    openFolder(product);
    EXPECT_EQ(itemTexts(properties(product, *tree(product))["items"]), (std::vector<std::string>{"src", "README.md"}));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT root_path FROM code_editor__workspaces WHERE active = 1") == std::vector<json>{{{"root_path", root().generic_string()}}}; }));
    // clang-format on

    // An edited document is marked until it is saved with the shortcut, and the file then holds the new text.
    openFile(product, "README.md");
    ASSERT_TRUE(editor(product, "# Workpane\n").has_value());
    product.frame();
    product.type("Edited ");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return documentTitles(product) == std::vector<std::string>{"README.md *"}; }));
    // clang-format on
    product.press(HeadlessProduct::command() | ImGuiKey_S);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return read("README.md") == "Edited # Workpane\n" && documentTitles(product) == std::vector<std::string>{"README.md"}; }));
    ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT path FROM code_editor__documents WHERE active = 1") == std::vector<json>{{{"path", path("README.md")}}}; }));
    // clang-format on

    // Closing a document with unsaved changes asks first, and cancelling keeps it open.
    product.type("Unsaved ");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return documentTitles(product) == std::vector<std::string>{"README.md *"}; }));
    // clang-format on
    product.emit(view, *documentTabs(product), "close", {{"id", path("README.md")}});
    product.frame();
    ASSERT_TRUE(product.answerDialog("cancel"));
    product.frame();
    EXPECT_EQ(documentTitles(product), std::vector<std::string>{"README.md *"});
    product.emit(view, *documentTabs(product), "close", {{"id", path("README.md")}});
    product.frame();
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return documentTitles(product).empty() && product.query("SELECT path FROM code_editor__documents").empty(); }));
    // clang-format on
    EXPECT_EQ(read("README.md"), "Edited # Workpane\n");
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// Saving and closing use the text on screen at once, a save keeps the permissions of the file it replaces, and a document whose file was removed outside the product writes it again.
TEST_F(CodeEditorPluginTest, SavesAndClosesWithTheTextOnScreen) {
    write("run.sh", "echo one\n");
    const std::filesystem::path script = root() / "run.sh";
    std::filesystem::permissions(script, std::filesystem::perms::owner_all | std::filesystem::perms::group_read | std::filesystem::perms::group_exec, std::filesystem::perm_options::replace);
    const std::filesystem::perms before = std::filesystem::status(script).permissions();
    HeadlessProduct product(data());
    boot(product);
    openFolder(product);
    openFile(product, "run.sh");
    ASSERT_TRUE(editor(product, "echo one\n").has_value());
    product.frame();

    // The save shortcut right after typing writes what was typed, before the editor would have reported it.
    product.type("# ");
    product.press(HeadlessProduct::command() | ImGuiKey_S);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return read("run.sh") == "# echo one\n"; }));
    // clang-format on
    EXPECT_EQ(std::filesystem::status(script).permissions(), before);

    // Closing right after typing asks first, because the edit is seen before the tab closes.
    product.type("x");
    product.emit(view, *documentTabs(product), "close", {{"id", path("run.sh")}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().shell().dialogs().active(); }));
    // clang-format on
    ASSERT_TRUE(product.answerDialog("cancel"));

    // A file removed outside the product while its document holds changes is written again by the next save.
    std::filesystem::remove(script);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return toasts(product) > 0U; }));
    // clang-format on
    product.press(HeadlessProduct::command() | ImGuiKey_S);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return read("run.sh") == "# xecho one\n" && documentTitles(product) == std::vector<std::string>{"run.sh"}; }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// The open folders and documents come back in their order, with the same ones active and the cursors where the reader left them.
TEST_F(CodeEditorPluginTest, RestoresTheFoldersAndDocumentsItHadOpen) {
    write("a.txt", "one\ntwo\nthree\n");
    write("b.txt", "bee\n");

    {
        HeadlessProduct product(data());
        boot(product);
        openFolder(product);
        openFile(product, "a.txt");
        product.frame();
        product.press(ImGuiKey_DownArrow);
        product.press(ImGuiKey_DownArrow);
        product.press(ImGuiKey_RightArrow);
        openFile(product, "b.txt");
        const std::vector<json> expected{{{"path", path("a.txt")}, {"cursor_line", 3}, {"cursor_column", 2}, {"active", 0}}, {{"path", path("b.txt")}, {"cursor_line", 1}, {"cursor_column", 1}, {"active", 1}}};
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return storedDocuments(product) == expected; }));
        // clang-format on
        EXPECT_TRUE(SurfaceReader(product.declared(view)).showsKey("code-editor.status.cursor"));
        product.stop();
        EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    }

    HeadlessProduct product(data());
    boot(product);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return documentTitles(product) == std::vector<std::string>{"a.txt", "b.txt"}; }));
    // clang-format on
    EXPECT_EQ(properties(product, *documentTabs(product))["current"], path("b.txt"));
    EXPECT_TRUE(editor(product, "one\ntwo\nthree\n").has_value());
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A cursor moved in the last moment before the product closes is written before the plugin stops, instead of being lost with the delay of its write.
TEST_F(CodeEditorPluginTest, KeepsTheLastChangeWhenItStopsAtOnce) {
    write("a.txt", "one\ntwo\n");
    HeadlessProduct product(data());
    boot(product);
    openFolder(product);
    openFile(product, "a.txt");
    const std::vector<json> opened{{{"path", path("a.txt")}, {"cursor_line", 1}, {"cursor_column", 1}, {"active", 1}}};
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return storedDocuments(product) == opened; }));
    // clang-format on

    product.press(ImGuiKey_DownArrow);
    product.stop();

    const std::vector<json> moved{{{"path", path("a.txt")}, {"cursor_line", 2}, {"cursor_column", 1}, {"active", 1}}};
    EXPECT_EQ(storedDocuments(product), moved);
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A folder stored at the first migration, even one whose update time stands before its creation after the clock was set back, opens again once the migration that drops those times ran.
TEST_F(CodeEditorPluginTest, OpensAFolderStoredBeforeItsTimesWereDropped) {
    write("a.txt", "one\n");
    {
        auto opened = persistence::DatabaseBootstrap::open(data());
        ASSERT_TRUE(opened.hasValue());
        persistence::Database& database = opened.value().database;
        const std::vector<std::vector<std::string>> first{{"CREATE TABLE code_editor__workspaces(id TEXT PRIMARY KEY NOT NULL, root_path TEXT NOT NULL UNIQUE, position INTEGER NOT NULL UNIQUE CHECK(position >= 0), active INTEGER NOT NULL CHECK(active IN (0, 1)), created_at_utc TEXT NOT NULL, updated_at_utc TEXT NOT NULL) STRICT", "CREATE TABLE code_editor__documents(workspace_id TEXT NOT NULL REFERENCES code_editor__workspaces(id) ON DELETE CASCADE, path TEXT NOT NULL, position INTEGER NOT NULL CHECK(position >= 0), cursor_line INTEGER NOT NULL CHECK(cursor_line >= 1), cursor_column INTEGER NOT NULL CHECK(cursor_column >= 1), active INTEGER NOT NULL CHECK(active IN (0, 1)), PRIMARY KEY(workspace_id, path), UNIQUE(workspace_id, position)) STRICT", "CREATE UNIQUE INDEX code_editor__active_workspace ON code_editor__workspaces(active) WHERE active = 1", "CREATE UNIQUE INDEX code_editor__active_document ON code_editor__documents(workspace_id, active) WHERE active = 1"}};
        ASSERT_TRUE(persistence::PluginDatabase::migrate(database, "code-editor", first).hasValue());
        ASSERT_TRUE(database.run("INSERT INTO code_editor__workspaces(id, root_path, position, active, created_at_utc, updated_at_utc) VALUES('kept', ?, 0, 1, '2099-01-01T00:00:00.000Z', '2026-01-01T00:00:00.000Z')", {root().generic_string()}).hasValue());
        ASSERT_TRUE(database.run("INSERT INTO code_editor__documents(workspace_id, path, position, cursor_line, cursor_column, active) VALUES('kept', ?, 0, 1, 1, 1)", {path("a.txt")}).hasValue());
    }

    HeadlessProduct product(data());
    boot(product);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return documentTitles(product) == std::vector<std::string>{"a.txt"}; }));
    // clang-format on
    EXPECT_TRUE(editor(product, "one\n").has_value());
    EXPECT_EQ(product.query("SELECT version FROM core_plugin_schemas WHERE plugin_id = 'code-editor'").front()["version"], 2);
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(CodeEditorPluginTest, RefusesToStartOverAStoredStateThatBreaksTheRules) {
    write("a.txt", "one\n");

    {
        HeadlessProduct product(data());
        boot(product);
        openFolder(product);
        openFile(product, "a.txt");
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return storedDocuments(product).size() == 1U; }));
        // clang-format on
        product.stop();
    }

    {
        auto database = persistence::Database::open(data() / persistence::DatabaseBootstrap::databaseName);
        ASSERT_TRUE(database.hasValue());
        ASSERT_TRUE(database.value().run("UPDATE code_editor__documents SET path = '/elsewhere/a.txt'").hasValue());
    }

    HeadlessProduct product(data());
    ASSERT_TRUE(product.boot().hasValue());
    EXPECT_FALSE(product.navigate("code-editor:editor").hasValue());

    // The reader hears which plugin could not read its data and where that data is kept.
    const std::string database = (data() / persistence::DatabaseBootstrap::databaseName).generic_string();
    // clang-format off
    EXPECT_TRUE(product.frameUntil([&]() { return product.product().shell().toasts().showing("A plugin could not be loaded", "The stored data of \"Code Editor\" could not be read, so it was not loaded. Its data is kept in \"" + database + "\""); }));
    // clang-format on
    product.stop();

    const auto stored = errors(product);
    ASSERT_EQ(stored.size(), 1U);
    EXPECT_NE(stored[0]["details_json"].get<std::string>().find("database_rows_invalid"), std::string::npos);
}

// A state the database refuses is told once the burst of changes that produced it settles, even when several changes came together.
TEST_F(CodeEditorPluginTest, TellsTheReaderAStateItCouldNotSave) {
    write("a.txt", "one\ntwo\nthree\n");

    {
        HeadlessProduct product(data());
        boot(product);
        openFolder(product);
        openFile(product, "a.txt");
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return storedDocuments(product).size() == 1U; }));
        // clang-format on
        product.stop();
    }

    {
        auto database = persistence::Database::open(data() / persistence::DatabaseBootstrap::databaseName);
        ASSERT_TRUE(database.hasValue());
        ASSERT_TRUE(database.value().run("CREATE TRIGGER refuse_documents BEFORE INSERT ON code_editor__documents BEGIN SELECT RAISE(ABORT, 'refused'); END").hasValue());
    }

    HeadlessProduct product(data());
    boot(product);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return documentTitles(product) == std::vector<std::string>{"a.txt"}; }));
    // clang-format on
    product.frame();
    product.press(ImGuiKey_DownArrow);
    product.press(ImGuiKey_DownArrow);
    product.press(ImGuiKey_RightArrow);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().shell().toasts().showing("Code Editor", "The code editor operation failed"); }));
    // clang-format on
    product.stop();

    const auto stored = errors(product);
    ASSERT_FALSE(stored.empty());
    EXPECT_EQ(stored.back()["message"], "The code editor state could not be saved");
}

// A file is read in the encoding its bytes spell, shown with that encoding in the status bar, and written back in another one when the reader asks.
TEST_F(CodeEditorPluginTest, ReadsAndWritesTheEncodingsItKnows) {
    write("latin.txt", "caf\xE9\n");
    write("wide.txt", std::string("\xFF\xFEh\0i\0\n\0", 8));
    write("naive.txt", "na\xC3\xAFve\n");
    write("binary.bin", std::string("\0\1\2", 3));
    write("utf32.txt", std::string("\xFF\xFE\0\0h\0\0\0", 8));
    HeadlessProduct product(data());
    boot(product);
    openFolder(product);

    openFile(product, "latin.txt");
    ASSERT_TRUE(editor(product, "caf\xC3\xA9\n").has_value());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return withText(product, "menuButton", "Latin-1").has_value(); }));
    // clang-format on
    product.emit(view, *withText(product, "menuButton", "Latin-1"), "select", {{"item", "save:utf-16le"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return read("latin.txt") == std::string("\xFF\xFE" "c\0a\0f\0\xE9\0\n\0", 12); }));
    ASSERT_TRUE(product.frameUntil([&]() { return withText(product, "menuButton", "UTF-16 LE").has_value(); }));
    // clang-format on

    openFile(product, "wide.txt");
    EXPECT_TRUE(editor(product, "hi\n").has_value());

    // A file read again in another encoding shows what its bytes spell there.
    openFile(product, "naive.txt");
    ASSERT_TRUE(editor(product, "na\xC3\xAFve\n").has_value());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return withText(product, "menuButton", "UTF-8").has_value(); }));
    // clang-format on
    product.emit(view, *withText(product, "menuButton", "UTF-8"), "select", {{"item", "reopen:latin1"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return editor(product, "na\xC3\x83\xC2\xAFve\n").has_value(); }));
    // clang-format on

    // A binary file and a file in UTF-32 are refused with a notification and open no tab.
    const std::size_t shown = toasts(product);
    product.emit(view, *tree(product), "activate", {{"id", path("binary.bin")}});
    product.emit(view, *tree(product), "activate", {{"id", path("utf32.txt")}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return toasts(product) == shown + 2; }));
    // clang-format on
    EXPECT_EQ(documentTitles(product), (std::vector<std::string>{"latin.txt", "wide.txt", "naive.txt"}));

    // An encoding that cannot write the text is refused and leaves the previous choice, so the next save still writes.
    write("emoji.txt", "smile \xF0\x9F\x98\x80\n");
    openFile(product, "emoji.txt");
    ASSERT_TRUE(editor(product, "smile \xF0\x9F\x98\x80\n").has_value());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return withText(product, "menuButton", "UTF-8").has_value(); }));
    // clang-format on
    const std::size_t refusals = toasts(product);
    product.emit(view, *withText(product, "menuButton", "UTF-8"), "select", {{"item", "save:latin1"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return toasts(product) == refusals + 1; }));
    // clang-format on
    product.settle(std::chrono::milliseconds(200));
    EXPECT_TRUE(withText(product, "menuButton", "UTF-8").has_value());
    EXPECT_EQ(read("emoji.txt"), "smile \xF0\x9F\x98\x80\n");
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// The EditorConfig files above a document decide its indentation and how it is written, and a change to one of them reaches the open documents.
TEST_F(CodeEditorPluginTest, FollowsTheEditorConfigFilesAboveEachDocument) {
    write(".editorconfig", "root = true\n\n[*.txt]\nindent_style = tab\nindent_size = 8\nend_of_line = crlf\ntrim_trailing_whitespace = true\ninsert_final_newline = true\n");
    write("notes/list.txt", "first  \nsecond");
    HeadlessProduct product(data());
    boot(product);
    openFolder(product);
    product.emit(view, *tree(product), "toggle", {{"id", path("notes")}, {"expanded", true}});
    openFile(product, "notes/list.txt");
    const auto document = editor(product, "first  \nsecond");
    ASSERT_TRUE(document.has_value());
    EXPECT_EQ(properties(product, *document)["tabSize"], 8);
    EXPECT_EQ(properties(product, *document)["insertSpaces"], false);
    EXPECT_TRUE(SurfaceReader(product.declared(view)).showsKey("code-editor.status.tab-size"));
    EXPECT_TRUE(SurfaceReader(product.declared(view)).showsText("CRLF"));

    product.frame();
    product.type("x");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return documentTitles(product) == std::vector<std::string>{"list.txt *"}; }));
    // clang-format on
    product.press(HeadlessProduct::command() | ImGuiKey_S);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return read("notes/list.txt") == "xfirst\r\nsecond\r\n"; }));
    // clang-format on

    // A nearer file overrides the root one once the poll notices it.
    write("notes/.editorconfig", "[*.txt]\nindent_style = space\nindent_size = 2\n");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return properties(product, *document)["tabSize"] == 2 && properties(product, *document)["insertSpaces"] == true; })) << properties(product, *document).dump().substr(0, 400);
    // clang-format on
    EXPECT_TRUE(SurfaceReader(product.declared(view)).showsKey("code-editor.status.space-size"));

    // Unsetting a property in the nearer file clears what the root file set, so the editor goes back to its own preferences.
    write("notes/.editorconfig", "[*.txt]\nindent_style = unset\nindent_size = unset\n");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return properties(product, *document)["tabSize"] != 2; })) << properties(product, *document).dump().substr(0, 400);
    // clang-format on
    EXPECT_NE(properties(product, *document)["tabSize"], 8);
    EXPECT_EQ(properties(product, *document)["insertSpaces"], true);
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A file is read in the charset its EditorConfig files name, as it is written, so a UTF-16 file without a byte order mark opens as its text.
TEST_F(CodeEditorPluginTest, ReadsAFileInTheCharsetItsEditorConfigNames) {
    write(".editorconfig", "root = true\n\n[*.txt]\ncharset = utf-16le\n");
    write("wide.txt", std::string("h\0i\0\n\0", 6));
    HeadlessProduct product(data());
    boot(product);
    openFolder(product);
    openFile(product, "wide.txt");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return editor(product, "hi\n").has_value(); }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A clean document follows its file when it changes outside the product and closes when the file disappears, while an edited one keeps its text.
TEST_F(CodeEditorPluginTest, FollowsFilesThatChangeOutsideTheProduct) {
    write("watched.txt", "first\n");
    write("edited.txt", "draft\n");
    HeadlessProduct product(data());
    boot(product);
    openFolder(product);
    openFile(product, "watched.txt");
    openFile(product, "edited.txt");
    product.frame();
    product.type("typed ");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return documentTitles(product) == std::vector<std::string>{"watched.txt", "edited.txt *"}; }));
    // clang-format on

    // A file read again keeps the cursor where the reader left it, which is stored once another document comes to the front.
    product.emit(view, *documentTabs(product), "select", {{"id", path("watched.txt")}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return editor(product, "first\n").has_value(); }));
    // clang-format on
    product.frame();
    product.press(ImGuiKey_RightArrow);
    product.press(ImGuiKey_RightArrow);
    product.settle(std::chrono::milliseconds(100));

    write("watched.txt", "second version\n");
    write("edited.txt", "changed elsewhere\n");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return editor(product, "second version\n").has_value(); }));
    // clang-format on
    product.settle(std::chrono::milliseconds(100));
    EXPECT_TRUE(editor(product, "typed draft\n").has_value());
    EXPECT_EQ(documentTitles(product), (std::vector<std::string>{"watched.txt", "edited.txt *"}));
    product.emit(view, *documentTabs(product), "select", {{"id", path("edited.txt")}});
    // clang-format off
    const auto cursorAt = [&](int column) { const auto stored = storedDocuments(product); return std::ranges::any_of(stored, [&](const json& entry) { return entry["path"] == path("watched.txt") && entry["cursor_line"] == 1 && entry["cursor_column"] == column; }); };
    EXPECT_TRUE(product.frameUntil([&]() { return cursorAt(3); }));
    // clang-format on

    std::filesystem::remove(root() / "watched.txt");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return documentTitles(product) == std::vector<std::string>{"edited.txt *"}; })) << ::testing::PrintToString(documentTitles(product));
    // clang-format on

    // The tree shows a file created outside the product and drops one removed there.
    write("appeared.txt", "new\n");
    // clang-format off
    const auto listed = [&]() { const auto files = tree(product, 0); return files.has_value() ? itemTexts(properties(product, *files)["items"]) : std::vector<std::string>{}; };
    ASSERT_TRUE(product.frameUntil([&]() { return listed() == std::vector<std::string>{"appeared.txt", "edited.txt"}; })) << ::testing::PrintToString(listed());
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// The tree creates, renames, moves and deletes inside the folder after the prompts and confirmations, and open documents follow every change.
TEST_F(CodeEditorPluginTest, CreatesRenamesAndDeletesFilesFromTheTree) {
    write("src/main.lua", "print('main')\n");
    write("docs/guide.md", "# Guide\n");
    HeadlessProduct product(data());
    boot(product);
    openFolder(product);

    product.emit(view, *tree(product), "menu", {{"item", "new-file"}});
    answerPrompt(product, "notes.txt");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return std::filesystem::exists(root() / "notes.txt") && documentTitles(product) == std::vector<std::string>{"notes.txt"}; }));
    // clang-format on

    product.emit(view, *tree(product), "item-menu", {{"id", path("notes.txt")}, {"item", "rename"}});
    answerPrompt(product, "renamed.txt");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return std::filesystem::exists(root() / "renamed.txt") && documentTitles(product) == std::vector<std::string>{"renamed.txt"}; })) << ::testing::PrintToString(documentTitles(product)) << std::filesystem::exists(root() / "renamed.txt");
    // clang-format on
    EXPECT_FALSE(std::filesystem::exists(root() / "notes.txt"));

    product.emit(view, *tree(product), "item-menu", {{"id", path("renamed.txt")}, {"item", "delete"}});
    product.frame();
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !std::filesystem::exists(root() / "renamed.txt") && documentTitles(product).empty(); }));
    // clang-format on

    // A moved file keeps its name in the folder the reader chose, and its open document follows it.
    openFile(product, "src/main.lua");
    product.dialogs().paths = {path("docs")};
    product.emit(view, *tree(product), "item-menu", {{"id", path("src/main.lua")}, {"item", "move"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return std::filesystem::exists(root() / "docs" / "main.lua") && properties(product, *documentTabs(product))["items"][0]["id"] == path("docs/main.lua"); }));
    ASSERT_TRUE(product.frameUntil([&]() { return storedDocuments(product).size() == 1U && storedDocuments(product)[0]["path"] == path("docs/main.lua"); }));
    // clang-format on
    EXPECT_FALSE(std::filesystem::exists(root() / "src" / "main.lua"));

    // The filter keeps the files whose names hold it with the folders that lead to them.
    const auto filter = nodes(product, "filterField");
    ASSERT_FALSE(filter.empty());
    product.emit(view, filter.front(), "change", {{"value", "main"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { const json items = properties(product, *tree(product))["items"]; return items.size() == 1U && items[0]["text"] == "docs" && items[0]["children"].size() == 1U; }));
    // clang-format on
    EXPECT_EQ(properties(product, *tree(product))["items"][0]["children"][0]["text"], "main.lua");

    // A file selected through the filter stays selected and in sight once the filter is cleared, with the folder above it opened.
    product.emit(view, *tree(product), "select", {{"id", path("docs/main.lua")}});
    product.frame();
    product.emit(view, filter.front(), "change", {{"value", ""}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return itemTexts(properties(product, *tree(product))["items"]) == std::vector<std::string>{"docs", "src"}; }));
    // clang-format on
    const json cleared = properties(product, *tree(product));
    EXPECT_EQ(cleared["selected"], path("docs/main.lua"));
    EXPECT_TRUE(cleared["items"][0]["expanded"].get<bool>());
    EXPECT_EQ(itemTexts(cleared["items"][0]["children"]), (std::vector<std::string>{"guide.md", "main.lua"}));
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// The finder ranks the files by the query and opens the chosen one, and the Search tab lists the lines that hold a text.
TEST_F(CodeEditorPluginTest, FindsFilesByNameAndLinesByText) {
    write("src/main.lua", "print('main')\n");
    write("src/manager.lua", "local manager = {}\n");
    write("docs/notes.md", "Printing notes\n");
    HeadlessProduct product(data());
    boot(product);
    openFolder(product);
    product.frame();

    product.press(HeadlessProduct::command() | ImGuiKey_P);
    const std::string finder = "dialog:code-editor:1";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { const auto lists = nodes(product, "list", finder); return !lists.empty() && properties(product, lists.front(), finder)["items"].size() == 3U; }));
    // clang-format on
    EXPECT_TRUE(SurfaceReader(product.declared(finder)).showsKey("code-editor.finder.count"));
    product.emit(finder, nodes(product, "textField", finder).front(), "change", {{"value", "mn"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return properties(product, nodes(product, "list", finder).front(), finder)["items"].size() == 2U; }));
    // clang-format on
    EXPECT_EQ(properties(product, nodes(product, "list", finder).front(), finder)["items"][0]["id"], "src/main.lua");
    product.emit(finder, nodes(product, "textField", finder).front(), "submit", {{"value", "mn"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !SurfaceReader(product.declared(finder)).mounted() && documentTitles(product) == std::vector<std::string>{"main.lua"}; }));
    // clang-format on

    const auto search = SurfaceReader(product.declared(view)).nodeShowing("code-editor.search.placeholder");
    ASSERT_TRUE(search.has_value());
    product.emit(view, *search, "submit", {{"value", "print"}});
    // clang-format off
    const auto results = [&]() { const auto tables = nodes(product, "table"); const auto found = std::ranges::find_if(tables, [&](ui::NodeId node) { return properties(product, node)["rows"].size() == 2U; }); return found != tables.end() ? std::optional<ui::NodeId>(*found) : std::nullopt; };
    ASSERT_TRUE(product.frameUntil([&]() { return results().has_value(); }));
    // clang-format on
    const json rows = properties(product, *results())["rows"];
    const json& notes = rows[0]["cells"][0] == "notes.md" ? rows[0] : rows[1];
    EXPECT_EQ(notes["cells"][2]["text"], "Printing notes");
    product.emit(view, *results(), "activate", {{"id", notes["id"]}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return documentTitles(product) == std::vector<std::string>{"main.lua", "notes.md"}; }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A language server found for the language of a document starts in the folder, and what it reports reaches the editor, the panels and the navigation.
TEST_F(CodeEditorPluginTest, SpeaksWithTheLanguageServerOfEachLanguage) {
    write("src/main.lua", "local function greet()\n  return x\nend\n");
    write("src/util.lua", "-- util\nlocal value = 2\n");
    answer("initialize", {{"capabilities", {{"textDocumentSync", 1}, {"completionProvider", json::object()}, {"hoverProvider", true}, {"definitionProvider", true}, {"referencesProvider", true}, {"documentSymbolProvider", true}, {"callHierarchyProvider", true}, {"workspaceSymbolProvider", true}, {"signatureHelpProvider", {{"triggerCharacters", {"("}}}}, {"semanticTokensProvider", {{"legend", {{"tokenTypes", {"function", "variable"}}, {"tokenModifiers", json::array()}}}, {"full", true}}}}}});
    const json start = {{"line", 0}, {"character", 15}};
    const json end = {{"line", 0}, {"character", 20}};
    const json unknown = {{"name", "later"}, {"kind", 99}, {"range", {{"start", start}, {"end", end}}}, {"selectionRange", {{"start", start}, {"end", end}}}};
    answer("textDocument/documentSymbol", json::array({{{"name", "greet"}, {"detail", ""}, {"kind", 12}, {"range", {{"start", start}, {"end", end}}}, {"selectionRange", {{"start", start}, {"end", end}}}, {"children", json::array({unknown})}}}));
    answer("textDocument/semanticTokens/full", {{"data", {0, 15, 5, 0, 0}}});
    answer("textDocument/completion", {{"isIncomplete", false}, {"items", json::array({{{"label", "greeting"}}})}});
    answer("textDocument/hover", {{"contents", "function greet()"}});
    answer("textDocument/definition", json::array({{{"uri", uri("src/util.lua")}, {"range", {{"start", {{"line", 1}, {"character", 6}}}, {"end", {{"line", 1}, {"character", 11}}}}}}}));
    answer("textDocument/references", json::array({{{"uri", uri("src/main.lua")}, {"range", {{"start", start}, {"end", end}}}}, {{"uri", uri("src/util.lua")}, {"range", {{"start", {{"line", 1}, {"character", 6}}}, {"end", {{"line", 1}, {"character", 11}}}}}}}));
    answer("textDocument/signatureHelp", {{"signatures", json::array({{{"label", "greet(name)"}}})}, {"activeSignature", 0}});
    answer("workspace/symbol", json::array({{{"name", "greet"}, {"kind", 12}, {"containerName", "main"}, {"location", {{"uri", uri("src/main.lua")}, {"range", {{"start", start}, {"end", end}}}}}}}));
    HeadlessProduct product(data());
    product.processes().executables["lua-language-server"] = RootedPath::of("opt/tools/lua-language-server");
    serve(product);
    boot(product);
    openFolder(product);
    openFile(product, "src/main.lua");

    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return received("textDocument/didOpen").size() == 1U; }));
    // clang-format on
    ASSERT_EQ(product.processes().launches.size(), 1U);
    EXPECT_EQ(product.processes().launches[0].program, RootedPath::of("opt/tools/lua-language-server"));
    EXPECT_EQ(product.processes().launches[0].directory, root());
    EXPECT_EQ(received("initialize")[0]["params"]["rootUri"], FileAddress::of(root()));
    EXPECT_EQ(received("initialize")[0]["params"]["processId"], 4242);
    EXPECT_EQ(received("initialize")[0]["params"]["capabilities"]["textDocument"]["publishDiagnostics"]["relatedInformation"], true);
    EXPECT_EQ(received("initialize")[0]["params"]["capabilities"]["textDocument"]["documentSymbol"]["symbolKind"]["valueSet"].size(), 26U);
    EXPECT_EQ(received("initialize")[0]["params"]["capabilities"]["workspace"]["symbol"]["symbolKind"]["valueSet"].size(), 26U);
    EXPECT_EQ(received("textDocument/didOpen")[0]["params"]["textDocument"]["languageId"], "lua");

    // What the server writes to its error stream is logged one line at a time, whatever pieces it arrives in.
    product.processes().events[0].output(process::ProcessStream::Error, "I[1] Starting LSP\nI[2] Buil");
    product.processes().events[0].output(process::ProcessStream::Error, "t preamble\r\n\n");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT message FROM logs__entries WHERE category = 'lsp' AND message LIKE 'I[%'").size() == 2U; }));
    // clang-format on
    const auto lines = product.query("SELECT message, details_json FROM logs__entries WHERE category = 'lsp' AND message LIKE 'I[%' ORDER BY sequence");
    EXPECT_EQ(lines[0]["message"], "I[1] Starting LSP");
    EXPECT_EQ(lines[1]["message"], "I[2] Built preamble");
    EXPECT_EQ(json::parse(lines[1]["details_json"].get<std::string>()), (json{{"language", "lua"}}));

    // The outline with the icon of the kind of each symbol, the semantic colors and the navigation the server offers reach the view, and a kind the protocol does not number shows no icon.
    const auto document = editor(product, "local function greet()\n  return x\nend\n");
    ASSERT_TRUE(document.has_value());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { const auto outline = tree(product, 1); return outline.has_value() && itemTexts(properties(product, *outline)["items"]) == std::vector<std::string>{"greet"}; }));
    ASSERT_TRUE(product.frameUntil([&]() { return properties(product, *document)["highlights"] == json::array({{{"line", 1}, {"column", 16}, {"length", 5}, {"role", "knownIdentifier"}}}); }));
    // clang-format on
    const json outlined = properties(product, *tree(product, 1))["items"];
    EXPECT_EQ(outlined[0]["icon"], "function");
    EXPECT_EQ(outlined[0]["iconColor"], "information");
    EXPECT_EQ(outlined[0]["children"][0]["text"], "later");
    EXPECT_FALSE(outlined[0]["children"][0].contains("icon"));
    std::vector<std::string> offered;
    const json menu = properties(product, *document)["menu"];

    for (const auto& item : menu) {
        offered.push_back(item["id"]);
    }

    EXPECT_EQ(offered, (std::vector<std::string>{"definition", "references", "incoming-calls", "outgoing-calls"}));
    EXPECT_EQ(properties(product, *document)["completion"], true);

    // Diagnostics become markers over their ranges, with their origin, code and related places, and rows of the Problems tab.
    const json related = json::array({{{"location", {{"uri", uri("src/util.lua")}, {"range", {{"start", {{"line", 1}, {"character", 6}}}, {"end", {{"line", 1}, {"character", 11}}}}}}}, {"message", "Declared here"}}});
    notify(product, 0, "textDocument/publishDiagnostics", {{"uri", uri("src/main.lua")}, {"diagnostics", json::array({{{"range", {{"start", {{"line", 1}, {"character", 9}}}, {"end", {{"line", 1}, {"character", 10}}}}}, {"severity", 1}, {"code", "undefined-global"}, {"source", "Lua Diagnostics."}, {"message", "Undefined global `x`."}, {"relatedInformation", related}}})}});
    const json marker = {{"line", 2}, {"column", 10}, {"endLine", 2}, {"endColumn", 11}, {"tone", "danger"}, {"message", "Undefined global `x`."}, {"detail", "Lua Diagnostics.(undefined-global)"}, {"related", json::array({{{"place", "util.lua:2:7"}, {"message", "Declared here"}}})}};
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return properties(product, *document)["markers"] == json::array({marker}); }));
    // clang-format on
    const auto tables = nodes(product, "table");
    // clang-format off
    const auto problems = std::ranges::find_if(tables, [&](ui::NodeId node) { const json rows = properties(product, node)["rows"]; return rows.size() == 2U && rows[0]["cells"][2] == "undefined-global" && rows[1]["cells"][0]["text"] == "util.lua"; });
    // clang-format on
    ASSERT_NE(problems, tables.end());

    // Completion and hover text are asked of the server, also for a word under a marker, since the editor shows the marker above the text.
    product.emit(view, *document, "complete-request", {{"request", 1}, {"line", 1}, {"column", 18}, {"word", "gre"}});
    product.emit(view, *document, "hover", {{"line", 1}, {"column", 17}, {"word", "greet"}});
    product.emit(view, *document, "hover", {{"line", 2}, {"column", 10}, {"word", "x"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return received("textDocument/completion").size() == 1U && received("textDocument/hover").size() == 2U; }));
    // clang-format on
    EXPECT_EQ(received("textDocument/completion")[0]["params"]["position"], (json{{"line", 0}, {"character", 17}}));
    EXPECT_EQ(received("textDocument/hover")[0]["params"]["position"], (json{{"line", 0}, {"character", 16}}));
    EXPECT_EQ(received("textDocument/hover")[1]["params"]["position"], (json{{"line", 1}, {"character", 9}}));

    // The definition opens its file, and the references fill the References tab.
    product.frame();
    product.press(ImGuiKey_F12);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return documentTitles(product) == std::vector<std::string>{"main.lua", "util.lua"} && received("textDocument/didOpen").size() == 2U; }));
    // clang-format on
    product.press(ImGuiMod_Shift | ImGuiKey_F12);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return std::ranges::any_of(nodes(product, "table"), [&](ui::NodeId node) { const json rows = properties(product, node)["rows"]; return rows.size() == 2U && rows[1]["cells"][0] == "util.lua"; }); }));
    // clang-format on

    // Typing in the symbol search asks every server of the folder.
    const auto symbols = SurfaceReader(product.declared(view)).nodeShowing("code-editor.symbols.search");
    ASSERT_TRUE(symbols.has_value());
    product.emit(view, *symbols, "change", {{"value", "gre"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return itemTexts(properties(product, *tree(product, 1))["items"]) == std::vector<std::string>{"main::greet"}; }));
    // clang-format on
    EXPECT_EQ(properties(product, *tree(product, 1))["items"][0]["icon"], "function");

    // A character the server names for signature help shows the active signature in the status bar.
    openFile(product, "src/util.lua");
    product.frame();
    product.type("f(");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(view)).showsText("greet(name)"); }));
    // clang-format on
    EXPECT_EQ(received("textDocument/signatureHelp")[0]["params"]["position"], (json{{"line", 1}, {"character", 8}}));

    // The related place of a problem opens its file at its line and column.
    product.emit(view, *problems, "activate", {{"id", "1.1"}});
    // clang-format off
    const auto opened = [&]() { const auto stored = storedDocuments(product); return std::ranges::any_of(stored, [&](const json& entry) { return entry["path"] == path("src/util.lua") && entry["cursor_line"] == 2 && entry["cursor_column"] == 7 && entry["active"] == 1; }); };
    ASSERT_TRUE(product.frameUntil(opened));
    // clang-format on

    product.stop();
    EXPECT_EQ(received("shutdown").size(), 1U);
    EXPECT_EQ(received("exit").size(), 1U);
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A request is built from the text on screen, a document coming back in front is analyzed again only when it changed, a problem keeps the related places the editor takes, and a search for servers that finds the same programs restarts none.
TEST_F(CodeEditorPluginTest, KeepsTheServersInStepWithWhatTheReaderSees) {
    write("main.lua", "local main = 1\n");
    write("util.lua", "local util = 2\n");
    answer("initialize", {{"capabilities", {{"textDocumentSync", 1}, {"completionProvider", json::object()}, {"documentSymbolProvider", true}}}});
    answer("textDocument/documentSymbol", json::array());
    answer("textDocument/completion", {{"isIncomplete", false}, {"items", json::array()}});
    HeadlessProduct product(data());
    product.processes().executables["lua-language-server"] = RootedPath::of("opt/tools/lua-language-server");
    serve(product);
    boot(product);
    openFolder(product);
    openFile(product, "main.lua");
    openFile(product, "util.lua");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return received("textDocument/documentSymbol").size() == 2U; }));
    // clang-format on

    // Coming back to a document that did not change asks the server for nothing.
    product.emit(view, *documentTabs(product), "select", {{"id", path("main.lua")}});
    product.emit(view, *documentTabs(product), "select", {{"id", path("util.lua")}});
    product.settle(std::chrono::milliseconds(100));
    EXPECT_EQ(received("textDocument/documentSymbol").size(), 2U);

    // Completion asked for right after typing reaches the server after the text that was typed.
    product.frame();
    product.type("zz");
    const auto document = editor(product, "local util = 2\n");
    ASSERT_TRUE(document.has_value());
    product.emit(view, *document, "complete-request", {{"request", 1}, {"line", 1}, {"column", 3}, {"word", "zz"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !received("textDocument/completion").empty(); }));
    // clang-format on
    const auto order = methods();
    const auto asked = std::ranges::find(order, "textDocument/completion");
    const auto changes = received("textDocument/didChange");
    ASSERT_FALSE(changes.empty());
    EXPECT_NE(std::ranges::find(order.begin(), asked, "textDocument/didChange"), asked);
    EXPECT_NE(changes.front()["params"]["contentChanges"][0]["text"].get<std::string>().find("zz"), std::string::npos);

    // A problem with more related places than the editor takes keeps the first ones and counts the rest in the last.
    json related = json::array();

    for (int place = 0; place < 70; ++place) {
        const json at = {{"line", 0}, {"character", place}};
        const json location = {{"uri", uri("main.lua")}, {"range", {{"start", at}, {"end", at}}}};
        related.push_back({{"location", location}, {"message", "Place " + std::to_string(place)}});
    }

    notify(product, 0, "textDocument/publishDiagnostics", {{"uri", uri("main.lua")}, {"diagnostics", json::array({{{"range", {{"start", {{"line", 0}, {"character", 6}}}, {"end", {{"line", 0}, {"character", 10}}}}}, {"severity", 2}, {"message", "Unused."}, {"relatedInformation", related}}})}});
    const auto mainDocument = editor(product, "local main = 1\n");
    ASSERT_TRUE(mainDocument.has_value());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { const json markers = properties(product, *mainDocument)["markers"]; return markers.size() == 1U && markers[0]["related"].size() == 64U; }));
    // clang-format on
    const json kept = properties(product, *mainDocument)["markers"][0]["related"];
    EXPECT_EQ(kept[62]["message"], "Place 62");
    EXPECT_EQ(kept[63]["message"], "7 more related places");

    // A search for servers that finds the same program keeps the server running.
    ASSERT_TRUE(product.navigate(ui::Shell::settingsDestination).hasValue());
    product.frame();
    product.frame();
    product.press(HeadlessProduct::command() | ImGuiKey_F);
    product.frame();
    product.type("Executable");
    const std::string section = "settings:code-editor:editor:language-servers";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(section)).nodeShowing("code-editor.actions.refresh").has_value(); }));
    // clang-format on
    click(product, "code-editor.actions.refresh", section);
    product.settle(std::chrono::milliseconds(300));
    EXPECT_EQ(product.processes().launches.size(), 1U);
    EXPECT_TRUE(received("shutdown").empty());
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A folder restored with its documents opens them and starts their servers only once the editor is first shown, and is stored as it was until then.
TEST_F(CodeEditorPluginTest, StartsARestoredFolderOnlyOnceTheEditorIsShown) {
    write("main.lua", "print('main')\n");
    answer("initialize", {{"capabilities", {{"textDocumentSync", 1}}}});

    {
        HeadlessProduct product(data());
        product.processes().executables["lua-language-server"] = RootedPath::of("opt/tools/lua-language-server");
        serve(product);
        boot(product);
        openFolder(product);
        openFile(product, "main.lua");
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return storedDocuments(product).size() == 1U && product.processes().launches.size() == 1U; }));
        // clang-format on
        product.stop();
    }

    {
        HeadlessProduct product(data());
        product.processes().executables["lua-language-server"] = RootedPath::of("opt/tools/lua-language-server");
        ASSERT_TRUE(product.boot().hasValue());
        product.settle(std::chrono::milliseconds(300));
        EXPECT_TRUE(product.processes().launches.empty());
        product.stop();
        EXPECT_EQ(storedDocuments(product).size(), 1U);
    }

    HeadlessProduct product(data());
    product.processes().executables["lua-language-server"] = RootedPath::of("opt/tools/lua-language-server");
    serve(product);
    boot(product);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return documentTitles(product) == std::vector<std::string>{"main.lua"} && product.processes().launches.size() == 1U; }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// Closing a folder asks about every changed document, even when a document before them leaves while a confirmation is open.
TEST_F(CodeEditorPluginTest, AsksAboutEveryChangedDocumentWhileAnotherOneLeaves) {
    write("a.txt", "a\n");
    write("b.txt", "b\n");
    write("c.txt", "c\n");
    HeadlessProduct product(data());
    boot(product);
    openFolder(product);
    openFile(product, "a.txt");
    openFile(product, "b.txt");
    product.frame();
    product.type("x");
    openFile(product, "c.txt");
    product.frame();
    product.type("y");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return documentTitles(product) == std::vector<std::string>{"a.txt", "b.txt *", "c.txt *"}; }));
    // clang-format on

    const std::string folder = properties(product, nodes(product, "tabs").front())["items"][0]["id"];
    product.emit(view, nodes(product, "tabs").front(), "close", {{"id", folder}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().shell().dialogs().active(); }));
    // clang-format on
    std::filesystem::remove(root() / "a.txt");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return documentTitles(product) == std::vector<std::string>{"b.txt *", "c.txt *"}; }));
    // clang-format on
    ASSERT_TRUE(product.answerDialog("confirm"));
    product.frame();
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().shell().dialogs().active(); }));
    // clang-format on
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT root_path FROM code_editor__workspaces").empty(); }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A file renamed only by the case of its letters takes the new name, even on a system that reads both names as one file.
TEST_F(CodeEditorPluginTest, RenamesAFileByTheCaseOfItsLetters) {
    write("notes.txt", "notes\n");
    HeadlessProduct product(data());
    boot(product);
    openFolder(product);
    product.emit(view, *tree(product), "item-menu", {{"id", path("notes.txt")}, {"item", "rename"}});
    answerPrompt(product, "Notes.txt");
    // clang-format off
    const auto named = [&]() { std::vector<std::string> names; for (const auto& entry : std::filesystem::directory_iterator(root())) { names.push_back(entry.path().filename().generic_string()); } return names; };
    ASSERT_TRUE(product.frameUntil([&]() { return named() == std::vector<std::string>{"Notes.txt"}; })) << ::testing::PrintToString(named());
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// Only the folder in front follows its files, and a folder behind it reads what changed once it comes to the front.
TEST_F(CodeEditorPluginTest, FollowsTheFilesOfTheFolderInFrontOnly) {
    TemporaryDirectory other;
    const std::filesystem::path second = std::filesystem::canonical(other.path());
    std::ofstream(second / "other.txt") << "other\n";
    write("first.txt", "first\n");
    HeadlessProduct product(data());
    boot(product);
    openFolder(product);
    openFile(product, "first.txt");
    product.dialogs().paths = {second.generic_string()};
    click(product, "code-editor.actions.open-folder");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT root_path FROM code_editor__workspaces WHERE active = 1") == std::vector<json>{{{"root_path", second.generic_string()}}}; }));
    // clang-format on

    write("first.txt", "changed behind\n");
    product.settle(std::chrono::milliseconds(1500));
    EXPECT_FALSE(editor(product, "changed behind\n").has_value());

    const std::string firstId = properties(product, nodes(product, "tabs").front())["items"][0]["id"];
    product.emit(view, nodes(product, "tabs").front(), "select", {{"id", firstId}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return editor(product, "changed behind\n").has_value(); }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// TSX and JSX files reach their server under the identifiers the protocol gives them.
TEST_F(CodeEditorPluginTest, NamesTsxAndJsxFilesTheWayTheirServerKnowsThem) {
    write("app.tsx", "export const App = () => null\n");
    write("view.jsx", "export const View = () => null\n");
    answer("initialize", {{"capabilities", {{"textDocumentSync", 1}}}});
    HeadlessProduct product(data());
    product.processes().executables["typescript-language-server"] = RootedPath::of("opt/tools/typescript-language-server");
    serve(product);
    boot(product);
    openFolder(product);
    openFile(product, "app.tsx");
    openFile(product, "view.jsx");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return received("textDocument/didOpen").size() == 2U; }));
    // clang-format on
    std::vector<std::string> ids;

    for (const auto& opened : received("textDocument/didOpen")) {
        ids.push_back(opened["params"]["textDocument"]["languageId"]);
    }

    std::ranges::sort(ids);
    EXPECT_EQ(ids, (std::vector<std::string>{"javascriptreact", "typescriptreact"}));
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// The settings reach every open document at once, and turning the language servers off stops them and hides what they reported.
TEST_F(CodeEditorPluginTest, AppliesItsSettingsToEveryOpenDocument) {
    write("main.lua", "print('main')\n");
    answer("initialize", {{"capabilities", {{"textDocumentSync", 1}}}});
    HeadlessProduct product(data());
    product.processes().executables["lua-language-server"] = RootedPath::of("opt/tools/lua-language-server");
    product.system().fonts = {{"Inter", Resources::fonts() / "Inter-Regular.ttf"}};
    serve(product);
    boot(product);
    openFolder(product);
    openFile(product, "main.lua");
    const auto document = editor(product, "print('main')\n");
    ASSERT_TRUE(document.has_value());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return received("initialized").size() == 1U && panelTabs(product) == std::vector<std::string>{"problems", "references", "search"}; }));
    // clang-format on

    // The status bar toggles word wrap for every document and stores it.
    const auto wrap = SurfaceReader(product.declared(view)).nodeShowing("code-editor.status.word-wrap");
    ASSERT_TRUE(wrap.has_value());
    product.emit(view, *wrap, "click", json::object());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return properties(product, *document)["wordWrap"] == true && properties(product, *wrap)["checked"] == true; }));
    // clang-format on
    EXPECT_TRUE(product.product().preferences().document("code-editor").value("wordWrap", false));

    ASSERT_TRUE(product.navigate(ui::Shell::settingsDestination).hasValue());
    product.frame();
    product.frame();
    product.press(HeadlessProduct::command() | ImGuiKey_F);
    product.frame();
    product.type("scheme");
    const std::string appearance = "settings:code-editor:editor:appearance";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(appearance)).mounted(); }));
    // clang-format on
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return properties(product, nodes(product, "combo", appearance).front(), appearance)["options"].size() == 2U; }));
    // clang-format on
    product.emit(appearance, nodes(product, "combo", appearance).front(), "change", {{"value", "Inter"}});
    product.emit(appearance, nodes(product, "combo", appearance)[1], "change", {{"value", "solarized-dark"}});
    product.emit(appearance, nodes(product, "numberField", appearance).front(), "change", {{"value", 14.0}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return properties(product, *document)["scheme"]["background"] == "#002b36" && properties(product, *document)["fontSize"] == 14 && properties(product, *document)["fontFamily"] == "Inter"; }));
    // clang-format on
    EXPECT_EQ(product.product().preferences().document("code-editor").value("colorScheme", ""), "solarized-dark");
    EXPECT_EQ(product.product().preferences().document("code-editor").value("fontFamily", ""), "Inter");

    // The zoom keys of a document change the size the settings show.
    product.emit(view, *document, "zoom", {{"fontSize", 16.0}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return properties(product, nodes(product, "numberField", appearance).front(), appearance)["value"] == 16; }));
    // clang-format on

    // The servers section lists the executable found for each language, and turning servers off stops them.
    product.press(HeadlessProduct::command() | ImGuiKey_A);
    product.type("executable");
    const std::string servers = "settings:code-editor:editor:language-servers";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(servers)).mounted(); }));
    // clang-format on
    const json rows = properties(product, nodes(product, "table", servers).front(), servers)["rows"];
    // clang-format off
    const auto lua = std::ranges::find_if(rows, [](const json& row) { return row["id"] == "lua"; });
    // clang-format on
    ASSERT_NE(lua, rows.end());
    EXPECT_EQ((*lua)["cells"][1]["text"], RootedPath::of("opt/tools/lua-language-server").generic_string());
    EXPECT_EQ(rows[0]["cells"][1]["text"]["key"], "code-editor.settings.not-found");
    product.emit(servers, nodes(product, "toggle", servers).front(), "change", {{"checked", false}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return received("shutdown").size() == 1U; }));
    // clang-format on
    ASSERT_TRUE(product.navigate("code-editor:editor").hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return panelTabs(product) == std::vector<std::string>{"search"}; }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// Another plugin opens a folder once through the capability, as the Terminal does for the folder its shell stands in.
TEST_F(CodeEditorPluginTest, OpensAFolderForAnotherPlugin) {
    HeadlessProduct product(data());
    ASSERT_TRUE(product.boot().hasValue());
    ASSERT_TRUE(product.navigate("terminal:workspace").hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.terminals().processes.empty() && SurfaceReader(product.declared("view:terminal:workspace")).mounted(); }));
    // clang-format on
    const std::string terminal = "view:terminal:workspace";
    product.emit(terminal, nodes(product, "terminal", terminal).front(), "directory", {{"path", root().generic_string()}});
    product.frame();

    for (int attempt = 0; attempt < 2; ++attempt) {
        product.emit(terminal, nodes(product, "menuButton", terminal).front(), "select", {{"item", "editor"}});
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT root_path FROM code_editor__workspaces") == std::vector<json>{{{"root_path", root().generic_string()}}}; }));
        // clang-format on
    }

    ASSERT_TRUE(SurfaceReader(product.declared(view)).mounted());
    const auto loaded = product.product().runtime().loadString(R"(
        local bridge = require("workpane.bridge")
        local task = require("workpane.task")
        local workpane = require("workpane.api").create("logs", "", { version = "", debug = false, platform = "", architecture = "", paths = { data = "" }, languages = {}, themes = {}, icons = {}, colors = {} })

        task.run("logs", "suite", function()
            local codes = {}

            for _, payload in ipairs({ { path = "/missing/folder" }, { path = "/", extra = true }, {} }) do
                local _, failure = workpane.capabilities.request("workspace.folder.open", payload):await()
                codes[#codes + 1] = failure.code
            end

            bridge.call("workpane_log", { plugin = "logs", level = "info", category = "suite", message = "codes:" .. table.concat(codes, ","), details = {} })
        end)
    )",
                                                               "capability");
    ASSERT_TRUE(loaded.hasValue()) << loaded.error().message;
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.query("SELECT message FROM logs__entries WHERE message = 'codes:code_editor_workspace_invalid,code_editor_request_invalid,code_editor_request_invalid'").empty(); }));
    // clang-format on
    product.stop();
}

// Several folders stay open in their own tabs, Save All writes the documents of the folder on screen, and closing a folder asks for its unsaved documents.
TEST_F(CodeEditorPluginTest, KeepsSeveralFoldersOpenInTabs) {
    TemporaryDirectory other;
    const std::filesystem::path second = std::filesystem::canonical(other.path());
    std::ofstream(second / "other.txt") << "other\n";
    write("first.txt", "first\n");
    HeadlessProduct product(data());
    boot(product);
    openFolder(product);
    openFile(product, "first.txt");
    product.frame();
    product.type("edited ");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return documentTitles(product) == std::vector<std::string>{"first.txt *"}; }));
    // clang-format on
    click(product, "code-editor.actions.save-all");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return read("first.txt") == "edited first\n"; }));
    // clang-format on
    product.type("again ");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return documentTitles(product) == std::vector<std::string>{"first.txt *"}; }));
    // clang-format on

    product.dialogs().paths = {second.generic_string()};
    click(product, "code-editor.actions.open-folder");
    // clang-format off
    const auto folders = [&]() { return itemTexts(properties(product, nodes(product, "tabs").front())["items"]); };
    ASSERT_TRUE(product.frameUntil([&]() { return folders() == std::vector<std::string>{root().filename().generic_string(), second.filename().generic_string()}; }));
    ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT root_path FROM code_editor__workspaces WHERE active = 1") == std::vector<json>{{{"root_path", second.generic_string()}}}; }));
    // clang-format on

    // Closing the first folder asks about its unsaved document, and cancelling keeps it open.
    const std::string firstId = properties(product, nodes(product, "tabs").front())["items"][0]["id"];
    product.emit(view, nodes(product, "tabs").front(), "close", {{"id", firstId}});
    product.frame();
    ASSERT_TRUE(product.answerDialog("cancel"));
    product.frame();
    EXPECT_EQ(folders().size(), 2U);

    // A close asked for twice while its confirmation is open asks once and closes that folder alone.
    product.emit(view, nodes(product, "tabs").front(), "close", {{"id", firstId}});
    product.emit(view, nodes(product, "tabs").front(), "close", {{"id", firstId}});
    product.frame();
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return folders() == std::vector<std::string>{second.filename().generic_string()} && product.query("SELECT root_path FROM code_editor__workspaces").size() == 1U; }));
    // clang-format on
    product.settle(std::chrono::milliseconds(200));
    EXPECT_FALSE(product.product().shell().dialogs().active());
    EXPECT_EQ(folders(), std::vector<std::string>{second.filename().generic_string()});
    EXPECT_EQ(read("first.txt"), "edited first\n");
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// What a server marks reaches the editor as decorations: code it calls unnecessary fades, a token it calls deprecated is struck through and the other uses of the symbol under the cursor are tinted, and a click with the modifier opens a definition.
TEST_F(CodeEditorPluginTest, DecoratesWhatTheLanguageServerMarks) {
    write("src/main.lua", "local old = 1\nprint(old)\n");
    write("src/util.lua", "-- util\nlocal value = 2\n");
    answer("initialize", {{"capabilities", {{"textDocumentSync", 1}, {"completionProvider", {{"triggerCharacters", {".", ":"}}}}, {"definitionProvider", true}, {"documentHighlightProvider", true}, {"semanticTokensProvider", {{"legend", {{"tokenTypes", {"variable"}}, {"tokenModifiers", {"readonly", "deprecated"}}}}, {"full", true}}}}}});
    answer("textDocument/semanticTokens/full", {{"data", {1, 6, 3, 0, 2}}});
    answer("textDocument/documentHighlight", json::array({{{"range", {{"start", {{"line", 0}, {"character", 6}}}, {"end", {{"line", 0}, {"character", 9}}}}}, {"kind", 3}}, {{"range", {{"start", {{"line", 1}, {"character", 6}}}, {"end", {{"line", 1}, {"character", 9}}}}}, {"kind", 2}}}));
    answer("textDocument/definition", json::array({{{"uri", uri("src/util.lua")}, {"range", {{"start", {{"line", 1}, {"character", 6}}}, {"end", {{"line", 1}, {"character", 11}}}}}}}));
    HeadlessProduct product(data());
    product.processes().executables["lua-language-server"] = RootedPath::of("opt/tools/lua-language-server");
    serve(product);
    boot(product);
    openFolder(product);
    openFile(product, "src/main.lua");
    const auto document = editor(product, "local old = 1\nprint(old)\n");
    ASSERT_TRUE(document.has_value());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return received("initialized").size() == 1U && properties(product, *document).value("definitions", false); }));
    // clang-format on
    EXPECT_EQ(properties(product, *document)["completionTriggers"], json::array({".", ":"}));

    const json struck = {{"line", 2}, {"column", 7}, {"endLine", 2}, {"endColumn", 10}, {"style", "struck"}};
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return properties(product, *document)["decorations"] == json::array({struck}); }));
    // clang-format on

    notify(product, 0, "textDocument/publishDiagnostics", {{"uri", uri("src/main.lua")}, {"diagnostics", json::array({{{"range", {{"start", {{"line", 0}, {"character", 6}}}, {"end", {{"line", 0}, {"character", 9}}}}}, {"severity", 4}, {"message", "Unused local `old`."}, {"tags", {1}}}})}});
    const json faded = {{"line", 1}, {"column", 7}, {"endLine", 1}, {"endColumn", 10}, {"style", "faded"}};
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return properties(product, *document)["decorations"] == json::array({faded, struck}); }));
    // clang-format on

    product.emit(view, *document, "cursor", {{"line", 1}, {"column", 8}, {"selection", ""}});
    const json occurrences = json::array({faded, struck, {{"line", 1}, {"column", 7}, {"endLine", 1}, {"endColumn", 10}, {"style", "occurrence"}}, {{"line", 2}, {"column", 7}, {"endLine", 2}, {"endColumn", 10}, {"style", "occurrence"}}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return properties(product, *document)["decorations"] == occurrences; }));
    // clang-format on
    EXPECT_EQ(received("textDocument/documentHighlight").back()["params"]["position"], (json{{"line", 0}, {"character", 7}}));

    product.emit(view, *document, "definition-request", {{"line", 2}, {"column", 8}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return documentTitles(product) == std::vector<std::string>{"main.lua", "util.lua"}; }));
    // clang-format on
    EXPECT_EQ(received("textDocument/definition")[0]["params"]["position"], (json{{"line", 1}, {"character", 7}}));

    // A file created outside the product is told to the server as a watched file.
    write("extra.lua", "return 1\n");
    const json created = {{"uri", uri("extra.lua")}, {"type", 1}};
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { const auto told = received("workspace/didChangeWatchedFiles"); return std::ranges::any_of(told, [&](const json& message) { return message["params"]["changes"] == json::array({created}); }); }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A language server that cannot be started is named to the reader.
TEST_F(CodeEditorPluginTest, TellsTheReaderALanguageServerCouldNotStart) {
    write("main.lua", "print('main')\n");
    HeadlessProduct product(data());
    product.processes().executables["lua-language-server"] = RootedPath::of("opt/tools/lua-language-server");
    product.processes().refusal = Error{"process_start_failed", "The program could not be started", RootedPath::of("opt/tools/lua-language-server").generic_string()};
    boot(product);
    openFolder(product);
    openFile(product, "main.lua");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().shell().toasts().showing("Code Editor", "The language server \"" + RootedPath::of("opt/tools/lua-language-server").generic_string() + "\" could not be started"); }));
    // clang-format on
    product.stop();
}

// A file opened twice at once, as a double activation of the tree asks, opens one document.
TEST_F(CodeEditorPluginTest, OpensOneDocumentForAFileOpenedTwiceAtOnce) {
    write("README.md", "# Title\n");
    HeadlessProduct product(data());
    boot(product);
    openFolder(product);
    const auto found = tree(product);
    ASSERT_TRUE(found.has_value());
    product.emit(view, *found, "activate", {{"id", path("README.md")}});
    product.emit(view, *found, "activate", {{"id", path("README.md")}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return documentTitles(product) == std::vector<std::string>{"README.md"}; }));
    // clang-format on
    product.settle(std::chrono::milliseconds(300));

    EXPECT_EQ(documentTitles(product), std::vector<std::string>{"README.md"});
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    product.stop();
}

// A language server that ends before it answers its initialization is started again under its restart budget instead of being reported as refusing it.
TEST_F(CodeEditorPluginTest, StartsAgainAServerThatEndedBeforeItsInitialization) {
    write("main.lua", "print('main')\n");
    HeadlessProduct product(data());
    product.processes().executables["lua-language-server"] = RootedPath::of("opt/tools/lua-language-server");
    // clang-format off
    product.processes().responder = [](std::size_t, std::string_view) {};
    // clang-format on
    boot(product);
    openFolder(product);
    openFile(product, "main.lua");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.processes().launches.size() == 1U; }));
    // clang-format on

    product.processes().ended[0] = true;
    product.processes().events[0].exited({1, false});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.processes().launches.size() == 2U; }));
    // clang-format on
    product.settle(std::chrono::milliseconds(200));

    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    product.stop();
}

// A server that keeps ending is started again five times within the window and then given up with its reason in the log.
TEST_F(CodeEditorPluginTest, GivesUpALanguageServerThatKeepsEnding) {
    write("main.lua", "print('main')\n");
    answer("initialize", {{"capabilities", {{"textDocumentSync", 1}}}});
    HeadlessProduct product(data());
    product.processes().executables["lua-language-server"] = RootedPath::of("opt/tools/lua-language-server");
    serve(product);
    boot(product);
    openFolder(product);
    openFile(product, "main.lua");

    for (std::size_t program = 0; program < 6; ++program) {
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.processes().launches.size() == program + 1; })) << program;
        // clang-format on
        product.processes().ended[program] = true;
        product.processes().events[program].exited({1, false});
    }

    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return errors(product).size() == 1U; }));
    // clang-format on
    product.settle(std::chrono::milliseconds(200));
    EXPECT_EQ(product.processes().launches.size(), 6U);
    EXPECT_NE(errors(product)[0]["message"].get<std::string>().find("restart limit"), std::string::npos);
    EXPECT_TRUE(product.product().shell().toasts().showing("Code Editor", "The language server ended with code 1 too many times and was given up"));
    product.stop();
}

} // namespace workpane::tests
