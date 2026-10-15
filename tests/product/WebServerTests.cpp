#include "app/Product.h"
#include "http/RequestRecord.h"
#include "http/StaticFileServer.h"
#include "persistence/Database.h"
#include "persistence/DatabaseBootstrap.h"
#include "persistence/PreferenceStore.h"
#include "scripting/ScriptRuntime.h"
#include "support/HeadlessProduct.h"
#include "support/LocalPort.h"
#include "support/SurfaceReader.h"
#include "support/TemporaryDirectory.h"
#include "ui/shell/Shell.h"

#include <gtest/gtest.h>
#include <httplib.h>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace workpane::tests {

using nlohmann::json;

// Drives the Web Server plugin through its page and its form, and reaches the servers it starts with a real HTTP client.
class WebServerTest : public ::testing::Test {
  protected:
    static constexpr std::string_view manager{"view:web-server:manager"};

    void SetUp() override {
        std::ofstream(m_site.path() / "index.html") << "<p>served</p>";
    }

    [[nodiscard]] std::filesystem::path site() const {
        return std::filesystem::canonical(m_site.path());
    }

    [[nodiscard]] const std::filesystem::path& data() const {
        return m_data.path();
    }

    [[nodiscard]] static std::string form(int number) {
        return "dialog:web-server:" + std::to_string(number);
    }

    static void boot(HeadlessProduct& product) {
        ASSERT_TRUE(product.boot().hasValue());
        ASSERT_TRUE(product.navigate("web-server:manager").hasValue());
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(manager)).showsKey("web-server.manager.title"); }));
        // clang-format on
    }

    static void click(HeadlessProduct& product, std::string_view surface, std::string_view key) {
        const auto node = SurfaceReader(product.declared(surface)).nodeShowing(key);
        ASSERT_TRUE(node.has_value()) << key;
        product.emit(surface, *node, "click", json::object());
    }

    // Opens a new form from the header of the page, which is the numbered dialog the plugin mounts next.
    static void openForm(HeadlessProduct& product, int number) {
        click(product, manager, "web-server.manager.new-server");
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(form(number))).mounted(); }));
        // clang-format on
    }

    // The fields are the name, the document root and the bind host in the order the form builds them, followed by the port.
    static void fill(HeadlessProduct& product, int number, std::string_view name, const std::filesystem::path& root, int port) {
        const std::string surface = form(number);
        const SurfaceReader reader(product.declared(surface));
        const auto fields = reader.nodes("textField");
        const auto paths = reader.nodes("pathField");
        const auto ports = reader.nodes("numberField");
        ASSERT_EQ(fields.size(), 2U);
        ASSERT_EQ(paths.size(), 1U);
        ASSERT_EQ(ports.size(), 1U);

        product.emit(surface, fields[0], "change", {{"value", std::string(name)}});
        product.emit(surface, paths[0], "change", {{"value", root.generic_string()}});
        product.emit(surface, fields[1], "change", {{"value", "127.0.0.1"}});
        product.emit(surface, ports[0], "change", {{"value", static_cast<double>(port)}});
        product.frame();
    }

    [[nodiscard]] static json tableRows(HeadlessProduct& product, std::size_t index) {
        const SurfaceReader reader(product.declared(manager));
        const auto tables = reader.nodes("table");
        return tables.size() > index ? reader.properties(tables[index])["rows"] : json::array();
    }

    [[nodiscard]] static bool listed(HeadlessProduct& product, std::string_view name, std::string_view stateKey) {
        for (const auto& row : tableRows(product, 0)) {
            if (row["cells"][1] == std::string(name) && row["cells"][0]["text"]["key"] == std::string(stateKey)) {
                return true;
            }
        }

        return false;
    }

    static void act(HeadlessProduct& product, std::string_view action) {
        const auto rows = tableRows(product, 0);
        ASSERT_FALSE(rows.empty());
        const auto table = SurfaceReader(product.declared(manager)).nodes("table").front();
        product.emit(manager, table, "action", {{"id", rows[0]["id"]}, {"action", std::string(action)}});
    }

    [[nodiscard]] static std::optional<std::string> fetch(int port, std::string_view path) {
        httplib::Client client("127.0.0.1", port);
        client.set_connection_timeout(std::chrono::seconds(2));
        const auto response = client.Get(std::string(path));

        if (!response || response->status != 200) {
            return std::nullopt;
        }

        return response->body;
    }

    [[nodiscard]] static std::vector<json> errors(const HeadlessProduct& product) {
        return product.query("SELECT source, category, message, details_json FROM logs__entries WHERE level = 'error'");
    }

  private:
    TemporaryDirectory m_data;
    TemporaryDirectory m_site;
};

TEST_F(WebServerTest, CancelsAPendingServerBeforeTheBindReplyArrives) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    const int port = LocalPort::free();
    product.recordCalls("workpane_log");
    const std::string script = R"(
        local bridge = require("workpane.bridge")
        local task = require("workpane.task")
        local started = bridge.request("workpane_http_serve", { plugin = "web-server", host = "127.0.0.1", port = )" +
                               std::to_string(port) + ", root = [==[" + site().generic_string() + R"(]==] })
        bridge.call("workpane_http_forget", { plugin = "web-server" })
        task.run("workpane", "test", function()
            local _, failure = started:await()
            bridge.call("workpane_log", { plugin = "web-server", level = "info", category = "test-http", message = "The server start ended", details = { code = failure ~= nil and failure.code or "" } })
        end)
    )";
    ASSERT_TRUE(product.product().runtime().loadString(script, "=pending-server").hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&product]() {
        const json call = product.lastCall("workpane_log");
        return call.is_object() && call.value("category", "") == "test-http";
    }));
    // clang-format on

    EXPECT_EQ(product.lastCall("workpane_log")["details"]["code"], "http_server_cancelled");
    // clang-format off
    auto reopened = http::StaticFileServer::start("127.0.0.1", port, site(), [](http::RequestRecord) {});
    // clang-format on
    ASSERT_TRUE(reopened.hasValue());
    reopened.value()->stop();
    product.stop();
}

TEST_F(WebServerTest, ServesAConfiguredFolderAndShowsTheRequestsItAnswers) {
    HeadlessProduct product(data());
    boot(product);
    EXPECT_TRUE(SurfaceReader(product.declared(manager)).showsKey("web-server.manager.empty"));

    const int port = LocalPort::free();
    openForm(product, 1);
    fill(product, 1, "Site", site(), port);
    ASSERT_TRUE(product.answerDialog("start"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !SurfaceReader(product.declared(form(1))).mounted() && listed(product, "Site", "web-server.manager.running"); }));
    // clang-format on

    EXPECT_EQ(fetch(port, "/index.html"), "<p>served</p>");
    EXPECT_EQ(fetch(port, "/"), "<p>served</p>");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return tableRows(product, 1).size() == 2U; }));
    // clang-format on

    const json requests = tableRows(product, 1);
    EXPECT_EQ(requests[0]["cells"][3], "/");
    EXPECT_EQ(requests[1]["cells"][3], "/index.html");
    EXPECT_EQ(requests[1]["cells"][1]["tone"], "success");
    EXPECT_EQ(requests[1]["cells"][2], "GET");

    // The log keeps the newest five hundred requests however many arrive at once, newest first.
    for (int request = 0; request < 505; ++request) {
        std::ignore = fetch(port, "/missing-" + std::to_string(request));
    }

    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { const json rows = tableRows(product, 1); return rows.size() == 500U && rows[0]["cells"][3] == "/missing-504"; }));
    // clang-format on
    EXPECT_EQ(tableRows(product, 1)[499]["cells"][3], "/missing-5");
    EXPECT_EQ(tableRows(product, 1)[0]["cells"][1]["tone"], "danger");

    const auto stored = product.query("SELECT name, root, bind_host, port, terminal_id FROM web_server__configurations");
    ASSERT_EQ(stored.size(), 1U);
    EXPECT_EQ(stored[0]["name"], "Site");
    EXPECT_EQ(stored[0]["root"], site().generic_string());
    EXPECT_EQ(stored[0]["port"], port);
    EXPECT_TRUE(stored[0]["terminal_id"].is_null());

    act(product, "stop");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return listed(product, "Site", "web-server.manager.stopped"); }));
    // clang-format on
    EXPECT_FALSE(fetch(port, "/index.html").has_value());
    EXPECT_TRUE(tableRows(product, 1).empty());

    act(product, "start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return listed(product, "Site", "web-server.manager.running"); }));
    // clang-format on
    EXPECT_EQ(fetch(port, "/index.html"), "<p>served</p>");

    act(product, "stop");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return listed(product, "Site", "web-server.manager.stopped"); }));
    // clang-format on
    act(product, "remove");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().shell().dialogs().active(); }));
    // clang-format on
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return tableRows(product, 0).empty() && product.query("SELECT id FROM web_server__configurations").empty(); }));
    // clang-format on

    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A running server opens in the browser plugin, which shows its address in a new tab.
TEST_F(WebServerTest, OpensARunningServerInTheBrowserPlugin) {
    HeadlessProduct product(data());
    boot(product);
    const int port = LocalPort::free();
    openForm(product, 1);
    fill(product, 1, "Site", site(), port);
    ASSERT_TRUE(product.answerDialog("start"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return listed(product, "Site", "web-server.manager.running"); }));
    // clang-format on

    const auto actions = tableRows(product, 0)[0]["actions"];
    EXPECT_NE(actions.dump().find("\"browse\""), std::string::npos);
    act(product, "browse");
    const std::string address = "http://127.0.0.1:" + std::to_string(port) + "/";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.query("SELECT id FROM browser__tabs WHERE url = ? AND active = 1", {address}).empty(); }));
    // clang-format on
    EXPECT_EQ(product.product().shell().destination(), "browser:web");
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(WebServerTest, RefusesAStartItCannotMakeAndKeepsWhatWasTyped) {
    HeadlessProduct product(data());
    boot(product);
    openForm(product, 1);

    fill(product, 1, " ", site(), LocalPort::free());
    ASSERT_TRUE(product.answerDialog("start"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(form(1))).showsText("Enter a name for the Web Server"); }));
    // clang-format on

    fill(product, 1, "Missing", site() / "missing", LocalPort::free());
    ASSERT_TRUE(product.answerDialog("start"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(form(1))).showsText("The document root is not a readable directory"); }));
    // clang-format on
    EXPECT_TRUE(product.query("SELECT id FROM web_server__configurations").empty());

    // A port another server holds is refused after the configuration is saved, so the reader keeps what was typed.
    const int held = LocalPort::free();
    // clang-format off
    auto holder = http::StaticFileServer::start("127.0.0.1", held, site(), [](const http::RequestRecord&) {});
    // clang-format on
    ASSERT_TRUE(holder.hasValue());
    fill(product, 1, "Taken", site(), held);
    ASSERT_TRUE(product.answerDialog("start"));
    const std::string refusal = "Could not bind to \"127.0.0.1:" + std::to_string(held) + "\". The address may be unavailable or the port may already be in use";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(form(1))).showsText(refusal); }));
    // clang-format on
    EXPECT_EQ(product.query("SELECT name FROM web_server__configurations").at(0)["name"], "Taken");

    ASSERT_TRUE(product.answerDialog("close"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !SurfaceReader(product.declared(form(1))).mounted() && listed(product, "Taken", "web-server.manager.stopped"); }));
    // clang-format on

    holder.value()->stop();
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(WebServerTest, AsksBeforeUnlockingARunningServerFromItsForm) {
    HeadlessProduct product(data());
    boot(product);
    const int port = LocalPort::free();
    openForm(product, 1);
    fill(product, 1, "Locked", site(), port);
    ASSERT_TRUE(product.answerDialog("start"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return listed(product, "Locked", "web-server.manager.running"); }));
    // clang-format on

    // The confirmation opens above the form, and cancelling it leaves the server running.
    act(product, "edit");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(form(2))).mounted(); }));
    // clang-format on
    ASSERT_TRUE(product.answerDialog("edit"));
    product.settle(std::chrono::milliseconds(100));
    ASSERT_TRUE(product.answerDialog("cancel"));
    product.settle(std::chrono::milliseconds(100));
    EXPECT_TRUE(listed(product, "Locked", "web-server.manager.running"));
    EXPECT_TRUE(SurfaceReader(product.declared(form(2))).mounted());

    ASSERT_TRUE(product.answerDialog("edit"));
    product.settle(std::chrono::milliseconds(100));
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return listed(product, "Locked", "web-server.manager.stopped"); }));
    // clang-format on
    EXPECT_FALSE(fetch(port, "/index.html").has_value());
    EXPECT_TRUE(SurfaceReader(product.declared(form(2))).mounted());

    product.press(ImGuiKey_Escape);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !SurfaceReader(product.declared(form(2))).mounted(); }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(WebServerTest, KeepsItsServersAndSplitAcrossRestarts) {
    const int port = LocalPort::free();
    {
        HeadlessProduct product(data());
        boot(product);
        openForm(product, 1);
        fill(product, 1, "Kept", site(), port);
        ASSERT_TRUE(product.answerDialog("start"));
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return listed(product, "Kept", "web-server.manager.running"); }));
        // clang-format on

        // A split dragged beyond what the settings offer is stored at their bound.
        const auto splitter = SurfaceReader(product.declared(manager)).nodes("splitter").front();
        product.emit(manager, splitter, "resize", {{"ratio", 0.95}});
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.product().preferences().document("web-server").value("splitRatio", 0) == 850; }));
        // clang-format on
        // clang-format off
        EXPECT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(manager)).properties(splitter)["ratio"] == 0.85; }));
        // clang-format on
        product.stop();
    }

    // A restart lists the server stopped, because a server runs only while the reader asked it to in this session.
    HeadlessProduct product(data());
    boot(product);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return listed(product, "Kept", "web-server.manager.stopped"); }));
    // clang-format on
    const auto splitter = SurfaceReader(product.declared(manager)).nodes("splitter").front();
    EXPECT_DOUBLE_EQ(SurfaceReader(product.declared(manager)).properties(splitter)["ratio"].get<double>(), 0.85);
    EXPECT_FALSE(fetch(port, "/index.html").has_value());
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(WebServerTest, ChangesTheSplitFromItsSettingsSection) {
    HeadlessProduct product(data());
    boot(product);
    ASSERT_TRUE(product.navigate(ui::Shell::settingsDestination).hasValue());
    product.frame();
    product.press(HeadlessProduct::command() | ImGuiKey_F);
    product.frame();
    product.type("server list");
    const std::string section = "settings:web-server:web-server:general";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(section)).mounted(); }));
    // clang-format on

    const auto ratio = SurfaceReader(product.declared(section)).nodes("numberField").front();
    product.emit(section, ratio, "change", {{"value", 600.0}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().preferences().document("web-server").value("splitRatio", 0) == 600; }));
    // clang-format on

    // The page already built follows the section once the value is stored.
    const auto splitter = SurfaceReader(product.declared(manager)).nodes("splitter").front();
    // clang-format off
    EXPECT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(manager)).properties(splitter)["ratio"] == 0.6; }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

TEST_F(WebServerTest, PreservesTheCompletePosixFolderNameWhenAskedToServeIt) {
#if defined(_WIN32)
    GTEST_SKIP() << "A backslash is a path separator on Windows";
#else
    const auto root = site() / "site\\";
    std::filesystem::create_directory(root);
    std::ofstream(root / "index.html") << "<p>complete folder</p>";
    HeadlessProduct product(data());
    boot(product);
    product.recordCalls("workpane_log");
    const auto script = R"(
        local bridge = require("workpane.bridge")
        local task = require("workpane.task")
        local workpane = require("workpane.api").create("logs", "", { version = "", debug = false, platform = "", architecture = "", paths = { data = "" }, languages = {}, themes = {}, icons = {}, colors = {} })

        task.run("logs", "suite", function()
            local _, failure = workpane.capabilities.request("workspace.folder.serve", { path = [==[)" +
                        root.generic_string() + R"(]==] }):await()
            bridge.call("workpane_log", { plugin = "logs", level = "info", category = "folder-name", message = "The folder request settled", details = { code = failure ~= nil and failure.code or "" } })
        end)
    )";
    ASSERT_TRUE(product.product().runtime().loadString(script, "=serve-folder-name").hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&product]() {
        const json call = product.lastCall("workpane_log");

        return SurfaceReader(product.declared(form(1))).mounted() || (call.is_object() && call.value("category", "") == "folder-name");
    }));
    // clang-format on

    const SurfaceReader reader(product.declared(form(1)));
    ASSERT_TRUE(reader.mounted()) << product.lastCall("workpane_log").dump();
    const auto fields = reader.nodes("pathField");
    ASSERT_EQ(fields.size(), 1U);
    EXPECT_EQ(reader.properties(fields[0])["value"], root.generic_string());
    const int port = LocalPort::free();
    fill(product, 1, "Complete folder", root, port);
    ASSERT_TRUE(product.answerDialog("start"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&product]() { return listed(product, "Complete folder", "web-server.manager.running"); }));
    // clang-format on
    EXPECT_EQ(fetch(port, "/index.html"), "<p>complete folder</p>");
    product.stop();
#endif
}

// A folder another plugin asks to serve is refused when it is missing or the request carries anything besides its path.
TEST_F(WebServerTest, RefusesToServeAFolderThatIsMissingOrAskedForWrongly) {
    HeadlessProduct product(data());
    boot(product);

    const auto loaded = product.product().runtime().loadString(R"(
        local bridge = require("workpane.bridge")
        local task = require("workpane.task")
        local workpane = require("workpane.api").create("logs", "", { version = "", debug = false, platform = "", architecture = "", paths = { data = "" }, languages = {}, themes = {}, icons = {}, colors = {} })

        task.run("logs", "suite", function()
            local codes = {}

            for _, payload in ipairs({ { path = "/missing/folder" }, { path = "/", extra = true }, {} }) do
                local _, failure = workpane.capabilities.request("workspace.folder.serve", payload):await()
                codes[#codes + 1] = failure.code
            end

            bridge.call("workpane_log", { plugin = "logs", level = "info", category = "suite", message = "codes:" .. table.concat(codes, ","), details = {} })
        end)
    )",
                                                               "capability");
    ASSERT_TRUE(loaded.hasValue()) << loaded.error().message;
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.query("SELECT message FROM logs__entries WHERE message = 'codes:web_server_root_invalid,web_server_request_invalid,web_server_request_invalid'").empty(); }));
    // clang-format on
    EXPECT_FALSE(SurfaceReader(product.declared(form(1))).mounted());
    product.stop();
}

TEST_F(WebServerTest, RecoversAStoredConfigurationThatBreaksTheRules) {
    {
        HeadlessProduct product(data());
        boot(product);
        product.stop();
    }

    {
        auto database = persistence::Database::open(data() / persistence::DatabaseBootstrap::databaseName);
        ASSERT_TRUE(database.hasValue());
        ASSERT_TRUE(database.value().run("INSERT INTO web_server__configurations(id, name, root, bind_host, port) VALUES('broken', ' Padded', '/', '127.0.0.1', 8080)").hasValue());
    }

    HeadlessProduct product(data());
    boot(product);
    EXPECT_TRUE(product.navigate("web-server:manager").hasValue());
    EXPECT_TRUE(std::filesystem::is_directory(data() / "plugin-data-recovery" / "web-server"));
    product.stop();
    EXPECT_TRUE(errors(product).empty());
}

} // namespace workpane::tests
