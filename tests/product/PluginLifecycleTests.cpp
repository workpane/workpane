#include "Error.h"
#include "app/Product.h"
#include "persistence/PreferenceStore.h"
#include "scripting/PluginRegistry.h"
#include "support/HeadlessProduct.h"
#include "support/ProductDatabase.h"
#include "support/SurfaceReader.h"
#include "support/TemporaryDirectory.h"
#include "ui/model/Surface.h"
#include "ui/shell/Shell.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::tests {

using nlohmann::json;

// Drives every case of the life of a plugin the reader turns off and on, and reads the changes of state the plugin manager writes to the log.
class PluginLifecycleTest : public ::testing::Test {
  protected:
    static constexpr std::string_view installed{"settings:workpane:plugins:installed"};

    // Writes a plugin of the test into a folder of plugins, with its catalog and the definition given.
    static void install(const std::filesystem::path& folder, const std::string& id, const std::string& definition) {
        std::filesystem::create_directories(folder / id);
        std::ofstream(folder / id / "plugin.lua") << definition;
        std::ofstream(folder / id / "translations.lua") << "return { en = { [\"" << id << ".plugin.title\"] = \"" << id << "\" }, pt = { [\"" << id << ".plugin.title\"] = \"" << id << "\" } }";
    }

    // Stores the core preferences a test starts from, such as the folder of its plugins and the plugins turned off.
    static void prepare(const TemporaryDirectory& data, const json& document) {
        auto database = tests::ProductDatabase::open(data);
        ASSERT_TRUE(persistence::PreferenceStore::store(database, "workpane", document).hasValue());
    }

    // Opens the Plugins group of the settings through the search, which is how the reader reaches it.
    static void openPlugins(HeadlessProduct& product) {
        ASSERT_TRUE(product.navigate(ui::Shell::settingsDestination).hasValue());
        product.frame();
        product.frame();
        product.press(HeadlessProduct::command() | ImGuiKey_F);
        product.frame();
        product.type("plugin");
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.surface(installed) != nullptr; }));
        // clang-format on
    }

    // Answers the control of the row of a plugin in the Plugins section, found by the title of the row: its switch or the button that erases its data.
    [[nodiscard]] static std::optional<ui::NodeId> control(HeadlessProduct& product, std::string_view titleKey, std::string_view kind) {
        const SurfaceReader reader(product.declared(installed));
        const auto rows = reader.nodes("settingsRow");
        const auto controls = reader.nodes(kind);

        for (std::size_t index = 0; index < rows.size() && index < controls.size(); ++index) {
            if (reader.properties(rows[index])["label"].value("key", "") == titleKey) {
                return controls[index];
            }
        }

        return std::nullopt;
    }

    static void turn(HeadlessProduct& product, std::string_view titleKey, bool on) {
        const auto found = control(product, titleKey, "toggle");
        ASSERT_TRUE(found.has_value()) << titleKey;
        product.emit(installed, *found, "change", {{"checked", on}});
    }

    [[nodiscard]] static bool switchable(HeadlessProduct& product, std::string_view titleKey) {
        return SurfaceReader(product.declared(installed)).properties(*control(product, titleKey, "toggle")).value("enabled", true);
    }

    [[nodiscard]] static bool checked(HeadlessProduct& product, std::string_view titleKey) {
        return SurfaceReader(product.declared(installed)).properties(*control(product, titleKey, "toggle")).value("checked", false);
    }

    [[nodiscard]] static bool running(HeadlessProduct& product, std::string_view id) {
        return product.product().plugins().find(std::string(id)) != nullptr;
    }

    // Answers the changes of state the log recorded for a plugin, each written as the state it left and the state it entered.
    [[nodiscard]] static std::vector<std::string> transitions(HeadlessProduct& product, std::string_view plugin) {
        std::vector<std::string> found;

        for (const auto& row : product.query("SELECT details_json FROM logs__entries WHERE source = 'workpane' AND category = 'plugins' AND message = 'A plugin changed its state' ORDER BY sequence")) {
            const json details = json::parse(row["details_json"].get<std::string>());

            if (details.value("plugin", "") == plugin) {
                found.push_back(details.value("from", "none") + ">" + details.value("to", ""));
            }
        }

        return found;
    }

    // Answers whether the recorded changes of state of a plugin end with the ones given, in that order.
    [[nodiscard]] static bool endsWith(const std::vector<std::string>& recorded, const std::vector<std::string>& last) {
        return recorded.size() >= last.size() && std::equal(last.begin(), last.end(), recorded.end() - static_cast<std::ptrdiff_t>(last.size()));
    }

    [[nodiscard]] static std::vector<json> errors(HeadlessProduct& product) {
        return product.query("SELECT source, message, details_json FROM logs__entries WHERE level = 'error'");
    }
};

// The Terminal builds its view ahead of time, and after the reader turns it off and on again the view comes back with its workspace and a new shell.
TEST_F(PluginLifecycleTest, BringsBackThePreloadedViewOfAPluginTurnedOffAndOn) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    const std::string view = "view:terminal:workspace";
    ASSERT_TRUE(product.navigate("terminal:workspace").hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(view) != nullptr && SurfaceReader(product.declared(view)).nodes("terminal").size() == 1U; }));
    // clang-format on

    openPlugins(product);
    turn(product, "terminal.plugin.title", false);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !running(product, "terminal") && product.surface(view) == nullptr; }));
    // clang-format on
    turn(product, "terminal.plugin.title", true);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(view) != nullptr && SurfaceReader(product.declared(view)).nodes("terminal").size() == 1U; }));
    // clang-format on
    EXPECT_EQ(product.terminals().processes.size(), 2U);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return endsWith(transitions(product, "terminal"), {"running>stopping", "stopping>disabled", "disabled>waiting", "waiting>starting", "starting>running"}); }));
    // clang-format on
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    product.stop();
}

// The games and the gallery wait for the reader to turn them on in every build, and a plugin turned on is remembered across a restart while the others stay off.
TEST_F(PluginLifecycleTest, KeepsThePluginsOffByDefaultUntilTheReaderTurnsThemOn) {
    TemporaryDirectory data;
    {
        HeadlessProduct product(data.path());
        ASSERT_TRUE(product.boot().hasValue());
        openPlugins(product);
        product.settle(std::chrono::milliseconds(200));
        EXPECT_FALSE(running(product, "task-hero"));
        EXPECT_FALSE(running(product, "flappy-bird"));
        EXPECT_FALSE(running(product, "components"));
        EXPECT_TRUE(product.product().shell().bands().empty());
        EXPECT_FALSE(product.navigate("flappy-bird:game").hasValue());
        EXPECT_FALSE(product.navigate("components:gallery").hasValue());
        EXPECT_FALSE(checked(product, "task-hero.plugin.title"));
        EXPECT_FALSE(checked(product, "flappy-bird.plugin.title"));
        EXPECT_FALSE(checked(product, "components.plugin.title"));
        EXPECT_EQ(transitions(product, "task-hero"), (std::vector<std::string>{"none>discovered", "discovered>disabled"}));
        EXPECT_EQ(transitions(product, "components"), (std::vector<std::string>{"none>discovered", "discovered>disabled"}));

        turn(product, "task-hero.plugin.title", true);
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.surface("band:task-hero:adventure") != nullptr; }));
        // clang-format on
        EXPECT_EQ(product.product().preferences().document("workpane")["pluginSwitches"], json({{"task-hero", true}}));
        EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
        product.stop();
    }

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface("band:task-hero:adventure") != nullptr; }));
    // clang-format on
    EXPECT_FALSE(running(product, "flappy-bird"));
    product.stop();
}

// A band leaves the window with its plugin and comes back when the reader turns the plugin on again.
TEST_F(PluginLifecycleTest, BringsBackTheBandOfAPluginTurnedOffAndOn) {
    TemporaryDirectory data;
    prepare(data, {{"pluginSwitches", {{"task-hero", true}}}});
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    const std::string band = "band:task-hero:adventure";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(band) != nullptr; }));
    // clang-format on

    openPlugins(product);
    turn(product, "task-hero.plugin.title", false);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(band) == nullptr && product.product().shell().bands().empty(); }));
    // clang-format on
    turn(product, "task-hero.plugin.title", true);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(band) != nullptr && product.product().shell().bands().size() == 1U; }));
    // clang-format on
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    product.stop();
}

// A plugin turned off while its view is on screen leaves the reader on the first destination left, and its view and its settings section are built again once it is back.
TEST_F(PluginLifecycleTest, RebuildsTheViewAndTheSectionOfAPluginTurnedOffWhileOnScreen) {
    TemporaryDirectory data;
    prepare(data, {{"pluginSwitches", {{"flappy-bird", true}}}});
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    const std::string view = "view:flappy-bird:game";
    const std::string section = "settings:flappy-bird:flappy-bird:general";
    openPlugins(product);
    ASSERT_TRUE(product.navigate("flappy-bird:game").hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(view) != nullptr; }));
    // clang-format on

    turn(product, "flappy-bird.plugin.title", false);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !running(product, "flappy-bird") && product.surface(view) == nullptr; }));
    // clang-format on
    EXPECT_NE(product.product().shell().destination(), "flappy-bird:game");

    turn(product, "flappy-bird.plugin.title", true);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return running(product, "flappy-bird"); }));
    // clang-format on
    ASSERT_TRUE(product.navigate("flappy-bird:game").hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(view) != nullptr; }));
    // clang-format on
    ASSERT_TRUE(product.navigate(ui::Shell::settingsDestination).hasValue());
    product.frame();
    product.frame();
    product.press(HeadlessProduct::command() | ImGuiKey_F);
    product.frame();
    product.type("Bird");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(section) != nullptr; }));
    // clang-format on
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    product.stop();
}

// Every switch waits while its plugin starts, a request that arrives all the same waits for the start, the preloaded view asked for meanwhile is built once the start finishes, and the plugin then stops.
TEST_F(PluginLifecycleTest, SerializesASwitchPressedWhileThePluginStarts) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    install(folder.path(), "slow", R"(return { id = "slow", titleKey = "slow.plugin.title",
        navigation = { { id = "main", titleKey = "slow.plugin.title", icon = "file", placement = "primary", order = 960, preload = true, view = function() return workpane.ui.label({ text = "slow" }) end } },
        start = function() require("async").sleep(300):await() end })");
    prepare(data, {{"pluginFolders", json::array({folder.path().generic_string()})}, {"pluginSwitches", {{"slow", false}}}});

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    openPlugins(product);
    turn(product, "slow.plugin.title", true);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return endsWith(transitions(product, "slow"), {"waiting>starting"}); }));
    // clang-format on
    product.frame();
    EXPECT_FALSE(switchable(product, "slow.plugin.title"));
    EXPECT_FALSE(switchable(product, "logs.plugin.title"));
    turn(product, "slow.plugin.title", false);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return endsWith(transitions(product, "slow"), {"disabled>waiting", "waiting>starting", "starting>running", "running>stopping", "stopping>disabled"}); }));
    // clang-format on
    EXPECT_FALSE(running(product, "slow"));

    turn(product, "slow.plugin.title", true);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface("view:slow:main") != nullptr; }));
    // clang-format on
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    product.stop();
}

// A plugin whose start fails when the reader turns it on is refused and told, and nothing it asked for is built.
TEST_F(PluginLifecycleTest, RefusesAPluginWhoseStartFailsWhenItIsTurnedOn) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    install(folder.path(), "fragile", R"(return { id = "fragile", titleKey = "fragile.plugin.title",
        navigation = { { id = "main", titleKey = "fragile.plugin.title", icon = "file", placement = "primary", order = 961, preload = true, view = function() return workpane.ui.label({ text = "fragile" }) end } },
        start = function() error("the start failed") end })");
    prepare(data, {{"pluginFolders", json::array({folder.path().generic_string()})}, {"pluginSwitches", {{"fragile", false}}}});

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    openPlugins(product);
    turn(product, "fragile.plugin.title", true);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return endsWith(transitions(product, "fragile"), {"waiting>starting", "starting>refused"}); }));
    // clang-format on
    product.settle(std::chrono::milliseconds(200));
    EXPECT_FALSE(running(product, "fragile"));
    EXPECT_EQ(product.surface("view:fragile:main"), nullptr);
    EXPECT_TRUE(product.product().shell().toasts().showing("A plugin could not be loaded", "The plugin \"fragile\" was not loaded, and its features are unavailable"));
    ASSERT_EQ(errors(product).size(), 1U) << json(errors(product)).dump();
    product.stop();
}

// A plugin whose folder vanished while it was turned off is refused by name when the reader turns it on.
TEST_F(PluginLifecycleTest, RefusesAPluginWhoseFolderVanishedWhileItWasOff) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    install(folder.path(), "vanishing", R"(return { id = "vanishing", titleKey = "vanishing.plugin.title" })");
    prepare(data, {{"pluginFolders", json::array({folder.path().generic_string()})}, {"pluginSwitches", {{"vanishing", false}}}});

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    openPlugins(product);
    std::filesystem::remove_all(folder.path() / "vanishing");
    turn(product, "vanishing.plugin.title", true);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return endsWith(transitions(product, "vanishing"), {"disabled>refused"}); }));
    // clang-format on
    EXPECT_FALSE(running(product, "vanishing"));
    EXPECT_EQ(errors(product).size(), 1U);
    product.stop();
}

// Turning off a plugin others depend on stops it first so its switch never springs back, stops its dependents, keeps every switch waiting until the change ends and records the plugin once.
TEST_F(PluginLifecycleTest, StopsADependencyWithItsDependentsWhileEverySwitchWaits) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    install(folder.path(), "base", R"(return { id = "base", titleKey = "base.plugin.title" })");
    install(folder.path(), "child", R"(return { id = "child", titleKey = "child.plugin.title", dependencies = { "base" }, stop = function() require("async").sleep(400):await() end })");
    prepare(data, {{"pluginFolders", json::array({folder.path().generic_string()})}});

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    openPlugins(product);
    turn(product, "base.plugin.title", false);
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return endsWith(transitions(product, "child"), {"running>stopping"}); }));
    // clang-format on
    product.frame();
    EXPECT_EQ(transitions(product, "base").back(), "running>stopping");
    EXPECT_FALSE(checked(product, "base.plugin.title"));
    EXPECT_FALSE(switchable(product, "base.plugin.title"));
    EXPECT_FALSE(switchable(product, "child.plugin.title"));

    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return endsWith(transitions(product, "base"), {"running>stopping", "stopping>disabled"}) && endsWith(transitions(product, "child"), {"running>stopping", "stopping>waiting"}); }));
    // clang-format on
    product.frame();
    EXPECT_TRUE(switchable(product, "base.plugin.title"));
    EXPECT_TRUE(checked(product, "child.plugin.title"));
    EXPECT_EQ(product.product().preferences().document("workpane")["pluginSwitches"], json({{"base", false}}));
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    product.stop();
}

// A plugin the reader turned off stays off with its switch off when its code no longer reads, and only turning it on tells the failure.
TEST_F(PluginLifecycleTest, KeepsAPluginOffWhenItsCodeNoLongerReads) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    install(folder.path(), "broken", "return { id = ");
    prepare(data, {{"pluginFolders", json::array({folder.path().generic_string()})}, {"pluginSwitches", {{"broken", false}}}});

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    openPlugins(product);
    product.settle(std::chrono::milliseconds(200));
    EXPECT_TRUE(endsWith(transitions(product, "broken"), {"discovered>refused", "refused>disabled"}));
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();

    const SurfaceReader reader(product.declared(installed));
    const auto rows = reader.nodes("settingsRow");
    const auto toggles = reader.nodes("toggle");
    // clang-format off
    const auto row = std::ranges::find_if(rows, [&reader](ui::NodeId id) { return reader.properties(id)["label"] == "broken"; });
    // clang-format on
    ASSERT_NE(row, rows.end());
    const ui::NodeId toggle = toggles[static_cast<std::size_t>(row - rows.begin())];
    EXPECT_FALSE(reader.properties(toggle).value("checked", true));

    product.emit(installed, toggle, "change", {{"checked", true}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return endsWith(transitions(product, "broken"), {"disabled>refused"}) && errors(product).size() == 1U; }));
    // clang-format on
    product.stop();
}

// Closing the product while a plugin stops lets that stop finish and never runs it a second time.
TEST_F(PluginLifecycleTest, StopsEachPluginOnceWhenTheProductClosesDuringAChange) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    install(folder.path(), "lingering", R"(local store
        return { id = "lingering", titleKey = "lingering.plugin.title",
        start = function() store = workpane.preferences.define({ stops = { type = "integer", default = 0 } }) end,
        stop = function()
            workpane.await(store:set("stops", store:get("stops") + 1))
            require("async").sleep(300):await()
        end })");
    prepare(data, {{"pluginFolders", json::array({folder.path().generic_string()})}});

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    openPlugins(product);
    turn(product, "lingering.plugin.title", false);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return endsWith(transitions(product, "lingering"), {"running>stopping"}); }));
    // clang-format on
    product.stop();
    EXPECT_EQ(product.product().preferences().document("lingering").value("stops", 0), 1);
}

// The data of a plugin is offered for erasing only while the plugin stays off, never while it starts again.
TEST_F(PluginLifecycleTest, OffersToEraseTheDataOfAPluginOnlyWhileItIsOff) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    install(folder.path(), "patient", R"(return { id = "patient", titleKey = "patient.plugin.title", migrations = { { "CREATE TABLE patient__items(id INTEGER PRIMARY KEY) STRICT" } }, start = function()
        require("async").sleep(300):await()
    end })");
    prepare(data, {{"pluginFolders", json::array({folder.path().generic_string()})}});

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    openPlugins(product);
    // clang-format off
    const auto erasable = [&product]() { return SurfaceReader(product.declared(installed)).properties(*control(product, "patient.plugin.title", "button")).value("visible", false); };
    ASSERT_TRUE(product.frameUntil([&]() { return running(product, "patient"); }));
    // clang-format on
    turn(product, "patient.plugin.title", false);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !running(product, "patient") && erasable(); }));
    // clang-format on

    turn(product, "patient.plugin.title", true);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return endsWith(transitions(product, "patient"), {"waiting>starting"}); }));
    // clang-format on
    product.frame();
    EXPECT_FALSE(erasable());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return running(product, "patient"); }));
    // clang-format on
    product.frame();
    EXPECT_FALSE(erasable());
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    product.stop();
}

// Erasing the data of a plugin that is off and turning it on again starts it afresh, with its tables created again and its preferences back to their defaults.
TEST_F(PluginLifecycleTest, StartsAPluginAfreshAfterItsDataWasErased) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    install(folder.path(), "keeper", R"(return { id = "keeper", titleKey = "keeper.plugin.title", migrations = { { "CREATE TABLE keeper__items(id INTEGER PRIMARY KEY) STRICT" } }, start = function()
        local store = workpane.preferences.define({ count = { type = "integer", default = 0 } })
        workpane.log.info("keeper", tostring(store:get("count")))
        store:set("count", store:get("count") + 1)
    end })");
    prepare(data, {{"pluginFolders", json::array({folder.path().generic_string()})}});

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().preferences().document("keeper").value("count", 0) == 1; }));
    // clang-format on
    openPlugins(product);
    turn(product, "keeper.plugin.title", false);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !running(product, "keeper") && SurfaceReader(product.declared(installed)).properties(*control(product, "keeper.plugin.title", "button")).value("visible", false); }));
    // clang-format on
    product.emit(installed, *control(product, "keeper.plugin.title", "button"), "click", json::object());
    product.frame();
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().preferences().document("keeper").empty() && product.query("SELECT name FROM sqlite_master WHERE name = 'keeper__items'").empty(); }));
    // clang-format on

    // The second start reads the default count again, and its entry reaches the log a few frames after the start.
    turn(product, "keeper.plugin.title", true);
    const std::string counts = "SELECT message FROM logs__entries WHERE source = 'keeper' AND category = 'keeper' ORDER BY sequence";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().preferences().document("keeper").value("count", 0) == 1 && !product.query("SELECT name FROM sqlite_master WHERE name = 'keeper__items'").empty() && product.query(counts).size() == 2U; }));
    // clang-format on
    const auto counted = product.query(counts);
    EXPECT_EQ(counted[0]["message"], "0");
    EXPECT_EQ(counted[1]["message"], "0");
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    product.stop();
}

// A plugin the reader turned off stays off across a restart, is discovered and listed, and the product closes without an error while it is off.
TEST_F(PluginLifecycleTest, KeepsAPluginOffAcrossARestart) {
    TemporaryDirectory data;
    {
        HeadlessProduct product(data.path());
        ASSERT_TRUE(product.boot().hasValue());
        openPlugins(product);
        turn(product, "logs.plugin.title", false);
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return !running(product, "logs") && product.product().preferences().document("workpane")["pluginSwitches"] == json({{"logs", false}}); }));
        // clang-format on
        product.stop();
    }

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    product.settle(std::chrono::milliseconds(200));
    EXPECT_FALSE(running(product, "logs"));
    openPlugins(product);
    ASSERT_TRUE(control(product, "logs.plugin.title", "toggle").has_value());
    EXPECT_FALSE(SurfaceReader(product.declared(installed)).properties(*control(product, "logs.plugin.title", "toggle")).value("checked", true));
    product.stop();
}

} // namespace workpane::tests
