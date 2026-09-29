#include "app/Product.h"
#include "localization/Localization.h"
#include "logging/LogLevels.h"
#include "logging/LogService.h"
#include "persistence/Database.h"
#include "persistence/DatabaseBootstrap.h"
#include "persistence/PluginDatabase.h"
#include "persistence/PreferenceStore.h"
#include "process/ProcessStream.h"
#include "scripting/PluginRegistry.h"
#include "scripting/ScriptRuntime.h"
#include "support/FakeSystemInspector.h"
#include "support/HeadlessProduct.h"
#include "support/LocalPort.h"
#include "support/ProductDatabase.h"
#include "support/Resources.h"
#include "support/RootedPath.h"
#include "support/SurfaceReader.h"
#include "support/TemporaryDirectory.h"
#include "support/WaveFile.h"
#include "ui/Texture.h"
#include "ui/TextureCache.h"
#include "ui/model/RenderContext.h"
#include "ui/model/Surface.h"
#include "ui/model/SurfaceStore.h"
#include "ui/shell/DialogAnswer.h"
#include "ui/shell/DialogHost.h"
#include "ui/shell/DialogRequest.h"
#include "ui/shell/Shell.h"
#include "ui/shell/ToastOverlay.h"
#include "ui/theme/ThemeColorNames.h"

#include <gtest/gtest.h>
#include <httplib.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#if !defined(_WIN32)
#include <signal.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace workpane::tests {

using nlohmann::json;

// Reads what a headless product shows and what it stored, which is how the product tests observe it from the outside.
class ProductTest : public ::testing::Test {
  protected:
    static std::vector<json> storedErrors(const std::filesystem::path& data) {
        auto database = persistence::Database::open(data / persistence::DatabaseBootstrap::databaseName);
        EXPECT_TRUE(database.hasValue());
        auto rows = database.value().query("SELECT source, category, message, details_json FROM logs__entries WHERE level = 'error'");
        EXPECT_TRUE(rows.hasValue());

        return rows.hasValue() ? rows.value() : std::vector<json>{};
    }

    // Turns the gallery on in the core preferences, since it waits for the reader to turn it on.
    static void showGallery(const TemporaryDirectory& data) {
        auto database = tests::ProductDatabase::open(data);
        ASSERT_TRUE(persistence::PreferenceStore::store(database, "workpane", {{"pluginSwitches", {{"components", true}}}}).hasValue());
    }

    // Opens the settings and searches them, which is how the reader reaches one group among all of them.
    static void search(HeadlessProduct& product, std::string_view text) {
        ASSERT_TRUE(product.navigate(ui::Shell::settingsDestination).hasValue());
        product.frame();
        product.frame();
        product.press(HeadlessProduct::command() | ImGuiKey_F);
        product.frame();
        product.type(text);
    }

    static bool stored(const std::filesystem::path& data, std::string_view message) {
        auto database = persistence::Database::open(data / persistence::DatabaseBootstrap::databaseName);
        auto rows = database.value().query("SELECT message FROM logs__entries WHERE message = ?", {std::string(message)});
        return rows.hasValue() && !rows.value().empty();
    }
};

TEST_F(ProductTest, BootsEveryBundledPluginAndOffersItsDestinations) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    const auto booted = product.boot();
    ASSERT_TRUE(booted.hasValue()) << booted.error().code << " " << booted.error().message << " " << booted.error().detail;

    std::set<std::string> destinations;

    for (const auto& item : product.product().shell().navigation()) {
        destinations.insert(item.destination());
    }

    EXPECT_TRUE(destinations.contains("logs:viewer"));
    EXPECT_TRUE(destinations.contains("system-information:overview"));
    EXPECT_TRUE(destinations.contains("donate:support"));
    EXPECT_FALSE(destinations.contains("components:gallery"));
}

TEST_F(ProductTest, BuildsEveryGalleryPageWithoutWritingAnError) {
    TemporaryDirectory data;
    showGallery(data);
    {
        HeadlessProduct product(data.path());
        ASSERT_TRUE(product.boot().hasValue());
        ASSERT_TRUE(product.navigate("components:gallery").hasValue());
        const std::string surface = "view:components:gallery";
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.surface(surface) != nullptr; }));
        // clang-format on
        const auto list = product.firstNode(surface, "list");
        ASSERT_TRUE(list.has_value());

        const std::vector<std::string> categories{"buttons", "text-input", "selection", "numbers", "date-time", "color", "typography", "indicators", "lists", "tables", "tabs", "layout", "dialogs", "native-dialogs", "notifications", "icons", "theme-colors", "code-editor", "terminal", "web-view", "markdown"};

        for (const auto& category : categories) {
            product.emit(surface, *list, "select", {{"id", category}});
            const std::string key = "components." + category + ".description";
            // clang-format off
            EXPECT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(surface)).showsKey(key); })) << category;
            // clang-format on
            product.settle(std::chrono::milliseconds(30));
        }

        // The editor of the gallery that offers completion answers a request with proposals the editor takes.
        product.emit(surface, *list, "select", {{"id", "code-editor"}});
        product.settle(std::chrono::milliseconds(30));
        const SurfaceReader editors(product.declared(surface));

        for (const ui::NodeId editor : editors.nodes("codeEditor")) {
            if (editors.properties(editor).value("completion", false)) {
                product.emit(surface, editor, "complete-request", {{"request", 1}, {"line", 2}, {"column", 3}, {"word", "ta"}});
            }
        }

        product.settle(std::chrono::milliseconds(30));

        // The terminal of the gallery blinks its cursor once its checkbox asks for it.
        product.emit(surface, *list, "select", {{"id", "terminal"}});
        product.settle(std::chrono::milliseconds(30));
        const auto blink = SurfaceReader(product.declared(surface)).nodeShowing("components.terminal.blink");
        ASSERT_TRUE(blink.has_value());
        product.emit(surface, *blink, "change", {{"checked", true}});
        // clang-format off
        EXPECT_TRUE(product.frameUntil([&]() { const SurfaceReader reader(product.declared(surface)); return !reader.nodes("terminal").empty() && reader.properties(reader.nodes("terminal").front()).value("cursorBlink", 0) == 530; }));
        // clang-format on

        EXPECT_GE(product.webViews().created, 1);
        EXPECT_TRUE(std::ranges::find(product.webViews().navigations, "https://paulox.dev") != product.webViews().navigations.end());

        // An address the web view refuses is told on the event line of the page.
        product.emit(surface, *list, "select", {{"id", "web-view"}});
        product.settle(std::chrono::milliseconds(30));
        const auto address = SurfaceReader(product.declared(surface)).nodes("textField");
        ASSERT_FALSE(address.empty());
        product.emit(surface, address.front(), "submit", {{"value", "not an address"}});
        // clang-format off
        const auto told = [&](std::string_view name) { const SurfaceReader reader(product.declared(surface)); for (const auto label : reader.nodes("label")) { const json shown = reader.properties(label).value("text", json()); if (shown.is_object() && shown.value("key", "") == "components.events.last" && shown["args"][0] == json(name)) { return true; } } return false; };
        EXPECT_TRUE(product.frameUntil([&]() { return told("refused"); }));
        // clang-format on

        // A closed tab leaves the others in the order the reader dragged them to.
        product.emit(surface, *list, "select", {{"id", "tabs"}});
        product.settle(std::chrono::milliseconds(30));
        const SurfaceReader tabsPage(product.declared(surface));
        const auto strips = tabsPage.nodes("tabs");
        // clang-format off
        const auto closable = std::ranges::find_if(strips, [&](ui::NodeId node) { return tabsPage.properties(node).value("closable", false); });
        // clang-format on
        ASSERT_NE(closable, strips.end());
        const ui::NodeId strip = *closable;
        product.emit(surface, strip, "add", json::object());
        product.frame();
        product.emit(surface, strip, "move", {{"id", "document-3"}, {"index", 0}});
        product.frame();
        product.emit(surface, strip, "close", {{"id", "document-1"}});
        // clang-format off
        const auto order = [&]() { const json shown = SurfaceReader(product.declared(surface)).properties(strip); json ids = json::array(); for (const auto& item : shown["items"]) { ids.push_back(item["id"]); } return ids; };
        EXPECT_TRUE(product.frameUntil([&]() { return order() == json::array({"document-3", "document-2"}); })) << order().dump();
        // clang-format on
        product.settle(std::chrono::milliseconds(300));
    }

    for (const auto& error : storedErrors(data.path())) {
        ADD_FAILURE() << error.dump();
    }
}

// The Theme Colors page of the gallery paints or writes in every role of the palette, so it stays the visual reference of the whole theme.
TEST_F(ProductTest, TheThemeColorsPageShowsEveryRoleOfThePalette) {
    TemporaryDirectory data;
    showGallery(data);
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    ASSERT_TRUE(product.navigate("components:gallery").hasValue());
    const std::string surface = "view:components:gallery";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.firstNode(surface, "list").has_value(); }));
    // clang-format on
    product.emit(surface, *product.firstNode(surface, "list"), "select", {{"id", "theme-colors"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(surface)).showsKey("components.theme-colors.description"); }));
    // clang-format on
    const SurfaceReader reader(product.declared(surface));
    std::set<std::string> shown;

    for (const ui::NodeId card : reader.nodes("card")) {
        shown.insert(reader.properties(card).value("background", std::string()));
    }

    for (const ui::NodeId label : reader.nodes("label")) {
        shown.insert(reader.properties(label).value("color", std::string()));
    }

    for (const auto& [role, name] : ui::ThemeColorNames::all()) {
        EXPECT_TRUE(shown.contains(std::string(name))) << name;
    }
}

// The swatches beside the layout popover choose between themselves, so a click checks the one pressed and unchecks the other.
TEST_F(ProductTest, TheGallerySwatchesChooseBetweenThemselves) {
    TemporaryDirectory data;
    showGallery(data);
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    ASSERT_TRUE(product.navigate("components:gallery").hasValue());
    const std::string surface = "view:components:gallery";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(surface)).showsKey("components.buttons.description"); }));
    const auto checked = [&](ui::NodeId node) { return SurfaceReader(product.declared(surface)).properties(node).value("checked", false); };
    // clang-format on
    const auto swatches = SurfaceReader(product.declared(surface)).nodes("layoutSwatch");
    ASSERT_GE(swatches.size(), 7U);
    const ui::NodeId three = swatches[4];
    const ui::NodeId bottom = swatches[5];
    EXPECT_FALSE(checked(three));
    EXPECT_TRUE(checked(bottom));

    product.emit(surface, three, "click", json::object());
    // clang-format off
    EXPECT_TRUE(product.frameUntil([&]() { return checked(three) && !checked(bottom); }));
    // clang-format on
}

// A dialog keeps the size it opened with while a problem appears inside it and scrolls with its content clear of the scroll bar, and a button that does not close lets its plugin check the form first.
TEST_F(ProductTest, ADialogKeepsItsSizeAndChecksItsFormBeforeClosing) {
    TemporaryDirectory data;
    showGallery(data);
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    ASSERT_TRUE(product.navigate("components:gallery").hasValue());
    const std::string view = "view:components:gallery";
    const std::string dialog = "dialog:components:1";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(view) != nullptr; }));
    // clang-format on
    product.emit(view, *product.firstNode(view, "list"), "select", {{"id", "dialogs"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(view)).nodeShowing("components.dialogs.open-custom").has_value(); }));
    // clang-format on
    product.emit(view, *SurfaceReader(product.declared(view)).nodeShowing("components.dialogs.open-custom"), "click", json::object());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return ImGui::GetTopMostPopupModal() != nullptr && SurfaceReader(product.declared(dialog)).mounted(); }));
    // clang-format on
    product.settle(std::chrono::milliseconds(50));
    const ImVec2 opened = ImGui::GetTopMostPopupModal()->Size;
    const auto alert = product.firstNode(dialog, "alert");
    ASSERT_TRUE(alert.has_value());
    // clang-format off
    const auto shown = [&]() { return SurfaceReader(product.declared(dialog)).properties(*alert).value("visible", true); };
    // clang-format on
    EXPECT_FALSE(shown());

    ASSERT_TRUE(product.answerDialog("create"));
    EXPECT_TRUE(product.frameUntil(shown));
    product.settle(std::chrono::milliseconds(50));
    ASSERT_NE(ImGui::GetTopMostPopupModal(), nullptr);
    EXPECT_EQ(ImGui::GetTopMostPopupModal()->Size.x, opened.x);
    EXPECT_EQ(ImGui::GetTopMostPopupModal()->Size.y, opened.y);
    // clang-format off
    const auto scrolling = [](const ImGuiWindow* window) { return window->Active && window->ParentWindow == ImGui::GetTopMostPopupModal() && std::string_view(window->Name).find("##body") != std::string_view::npos; };
    // clang-format on
    const auto body = std::ranges::find_if(GImGui->Windows, scrolling);
    ASSERT_NE(body, GImGui->Windows.end());
    EXPECT_GT((*body)->ScrollMax.y, 0.0F);
    EXPECT_FLOAT_EQ((*body)->Scroll.y, (*body)->ScrollMax.y);
    // The body that scrolls keeps its content clear of the scroll bar, so the bar never covers a control.
    EXPECT_GE((*body)->InnerRect.GetWidth() - (*body)->ContentSize.x, 4.0F);

    product.emit(dialog, *product.firstNode(dialog, "textField"), "change", {{"value", "Write the report"}});
    product.frame();
    ASSERT_TRUE(product.answerDialog("create"));
    // clang-format off
    EXPECT_TRUE(product.frameUntil([&]() { return !SurfaceReader(product.declared(dialog)).mounted(); }));
    // clang-format on
}

TEST_F(ProductTest, AnswersTheShortcutsOfTheViewOnScreenOnly) {
    TemporaryDirectory data;
    showGallery(data);
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    ASSERT_TRUE(product.navigate("components:gallery").hasValue());
    const std::string surface = "view:components:gallery";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(surface)).showsKey("components.buttons.description"); }));
    // clang-format on

    product.press(ImGuiMod_Alt | ImGuiKey_DownArrow);
    // clang-format off
    EXPECT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(surface)).showsKey("components.text-input.description"); }));
    // clang-format on
    product.press(ImGuiMod_Alt | ImGuiKey_UpArrow);
    product.press(ImGuiMod_Alt | ImGuiKey_UpArrow);
    // clang-format off
    EXPECT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(surface)).showsKey("components.canvas.description"); }));
    // clang-format on

    // The shortcuts of the gallery do nothing while another view is on screen.
    ASSERT_TRUE(product.navigate("donate:support").hasValue());
    product.press(ImGuiMod_Alt | ImGuiKey_DownArrow);
    product.settle(std::chrono::milliseconds(100));
    EXPECT_TRUE(SurfaceReader(product.declared(surface)).showsKey("components.canvas.description"));
}

TEST_F(ProductTest, ChangesTheLanguageFromTheSettingsAndStoresIt) {
    TemporaryDirectory data;
    {
        HeadlessProduct product(data.path());
        ASSERT_TRUE(product.boot().hasValue());
        EXPECT_EQ(product.product().localization().language(), "en");
        search(product, "Language");
        const std::string surface = "settings:workpane:application:general";
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.surface(surface) != nullptr; }));
        // clang-format on
        const auto language = product.firstNode(surface, "combo");
        ASSERT_TRUE(language.has_value());

        product.emit(surface, *language, "change", {{"value", "pt"}});
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.product().localization().language() == "pt"; }));
        // clang-format on
        EXPECT_EQ(product.product().preferences().document("workpane")["language"], "pt");
        product.settle(std::chrono::milliseconds(200));
    }

    auto database = persistence::Database::open(data.path() / persistence::DatabaseBootstrap::databaseName);
    ASSERT_TRUE(database.hasValue());
    auto documents = persistence::PreferenceStore::load(database.value());
    ASSERT_TRUE(documents.hasValue());
    EXPECT_EQ(documents.value().documents.at("workpane")["language"], "pt");

    HeadlessProduct reopened(data.path());
    ASSERT_TRUE(reopened.boot().hasValue());
    EXPECT_EQ(reopened.product().localization().language(), "pt");
}

// Dialogs and toasts take the windows of their level and of their place, so every wave of them after the first leaves ImGui with the windows it already had.
TEST_F(ProductTest, ReusesTheWindowsOfItsDialogsAndToasts) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    // clang-format off
    const auto wave = [&product]() {
        for (int index = 0; index < 5; ++index) {
            ui::DialogRequest request;
            request.kind = ui::DialogKind::Prompt;
            request.title = "Name " + std::to_string(index);
            request.confirmText = "Save";
            request.cancelText = "Cancel";
            request.answer = [](const ui::DialogAnswer&) {};
            product.product().shell().dialogs().open(std::move(request));
            product.product().shell().toasts().show("Saved " + std::to_string(index), "", ui::Severity::Information, 0.0);
            ASSERT_TRUE(product.answerDialog("cancel"));
        }

        product.settle(std::chrono::milliseconds(100));
    };
    // clang-format on

    wave();
    const int windows = GImGui->Windows.Size;
    wave();
    wave();

    EXPECT_EQ(GImGui->Windows.Size, windows);
    product.stop();
}

// The configuration is exported where the reader chooses while both buttons wait, a second transfer asked for meanwhile is ignored, and an export over the live database is refused without touching it.
TEST_F(ProductTest, ExportsTheConfigurationOneTransferAtATimeAndNeverOverTheLiveDatabase) {
    TemporaryDirectory data;
    const std::filesystem::path exported = data.path() / "exported.sqlite3";
    const std::filesystem::path live = data.path() / persistence::DatabaseBootstrap::databaseName;
    {
        HeadlessProduct product(data.path());
        ASSERT_TRUE(product.boot().hasValue());
        search(product, "Export");
        const std::string surface = "settings:workpane:application:configuration";
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.surface(surface) != nullptr; }));
        // clang-format on
        const auto buttons = SurfaceReader(product.declared(surface)).nodes("button");
        ASSERT_EQ(buttons.size(), 2U);
        bool waited = false;

        product.dialogs().paths = {exported.generic_string()};
        product.emit(surface, buttons[1], "click", json::object());
        product.emit(surface, buttons[1], "click", json::object());

        while (!std::filesystem::exists(exported) || !SurfaceReader(product.declared(surface)).properties(buttons[0]).value("enabled", true)) {
            product.frame();
            waited = waited || !SurfaceReader(product.declared(surface)).properties(buttons[0]).value("enabled", true);
        }

        EXPECT_TRUE(waited);
        EXPECT_TRUE(storedErrors(data.path()).empty()) << json(storedErrors(data.path())).dump();

        product.dialogs().paths = {live.generic_string()};
        product.emit(surface, buttons[1], "click", json::object());
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return stored(data.path(), "The configuration cannot be exported over the database the product is using (code \"configuration_export_live\", detail \"" + live.generic_string() + "\")"); }));
        // clang-format on
        EXPECT_TRUE(product.product().shell().toasts().showing("Configuration", "The configuration could not be exported"));
        product.settle(std::chrono::milliseconds(100));
    }

    auto reopened = persistence::Database::open(live, true);
    ASSERT_TRUE(reopened.hasValue());
    EXPECT_TRUE(persistence::PreferenceStore::load(reopened.value()).hasValue());
    auto copy = persistence::Database::open(exported, true);
    ASSERT_TRUE(copy.hasValue());
    EXPECT_TRUE(persistence::PreferenceStore::load(copy.value()).hasValue());
}

// An import is proven against the migrations every installed plugin declares, so a schema newer than Flappy Bird reads is refused although the game is off and never ran here, and one it reads is staged for the next start.
TEST_F(ProductTest, ProvesAnImportAgainstTheMigrationsOfTheInstalledPlugins) {
    TemporaryDirectory data;
    TemporaryDirectory newerFolder;
    TemporaryDirectory readableFolder;
    std::filesystem::path newer;
    std::filesystem::path readable;
    {
        auto database = tests::ProductDatabase::open(newerFolder);
        ASSERT_TRUE(persistence::PluginDatabase::migrate(database, "flappy-bird", {{"CREATE TABLE flappy_bird__rounds(id INTEGER PRIMARY KEY) STRICT"}, {"ALTER TABLE flappy_bird__rounds ADD COLUMN score INTEGER"}}).hasValue());
        newer = database.file();
        auto other = tests::ProductDatabase::open(readableFolder);
        ASSERT_TRUE(persistence::PluginDatabase::migrate(other, "flappy-bird", {{"CREATE TABLE flappy_bird__rounds(id INTEGER PRIMARY KEY) STRICT"}}).hasValue());
        readable = other.file();
    }

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    search(product, "Export");
    const std::string surface = "settings:workpane:application:configuration";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(surface) != nullptr; }));
    // clang-format on
    const auto buttons = SurfaceReader(product.declared(surface)).nodes("button");
    const std::filesystem::path staged = data.path() / persistence::DatabaseBootstrap::stagedImportName;

    product.dialogs().paths = {newer.generic_string()};
    product.emit(surface, buttons[0], "click", json::object());
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return stored(data.path(), "The chosen database carries a plugin schema newer than the plugin installed here (code \"configuration_import_newer\", detail \"flappy-bird\")"); }));
    // clang-format on
    EXPECT_FALSE(std::filesystem::exists(staged));
    EXPECT_FALSE(product.product().restarting());

    product.dialogs().paths = {readable.generic_string()};
    product.emit(surface, buttons[0], "click", json::object());
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return std::filesystem::exists(staged) && product.product().restarting(); }));
    // clang-format on
    product.stop();
}

// The reader adds a folder of plugins in the settings and restarts, and the next start loads the plugins inside it after the bundled ones, refusing a plugin whose name is taken and a folder it cannot read.
TEST_F(ProductTest, LoadsThePluginsOfTheFoldersTheReaderAdds) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    const std::string surface = "settings:workpane:plugins:folders";
    const std::filesystem::path missing = folder.path() / "missing";

    for (const std::string id : {"notes", "logs"}) {
        std::filesystem::create_directories(folder.path() / id);
        std::ofstream(folder.path() / id / "plugin.lua") << "return { id = \"" << id << "\", titleKey = \"" << id << ".plugin.title\", navigation = { { id = \"main\", titleKey = \"" << id << ".plugin.title\", icon = \"file\", placement = \"primary\", order = 950, view = function() return workpane.ui.label({ text = workpane.i18n.text(\"" << id << ".plugin.title\") }) end } } }\n";
        std::ofstream(folder.path() / id / "translations.lua") << "return { en = { [\"" << id << ".plugin.title\"] = \"Notes\" }, pt = { [\"" << id << ".plugin.title\"] = \"Notas\" } }\n";
    }

    // A link to nothing among the plugins is refused by its name, and the plugins beside it still load.
    std::error_code linked;
    std::filesystem::create_directory_symlink(folder.path() / "nowhere", folder.path() / "broken", linked);

    {
        HeadlessProduct product(data.path());
        ASSERT_TRUE(product.boot().hasValue());
        search(product, "plugin");
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.surface(surface) != nullptr; }));
        // clang-format on
        const SurfaceReader reader(product.declared(surface));
        EXPECT_FALSE(reader.properties(reader.nodes("alert").front()).value("visible", true));
        EXPECT_TRUE(reader.properties(reader.nodes("emptyState").back()).value("visible", true));

        for (const auto& chosen : {folder.path(), missing, folder.path()}) {
            product.dialogs().paths = {chosen.generic_string()};
            product.emit(surface, *reader.nodeShowing("workpane.plugins.add"), "click", json::object());
            product.settle(std::chrono::milliseconds(50));
        }

        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(surface)).properties(reader.nodes("table").back())["rows"].size() == 2U; }));
        // clang-format on
        EXPECT_TRUE(SurfaceReader(product.declared(surface)).properties(reader.nodes("alert").front()).value("visible", false));
        EXPECT_EQ(product.product().preferences().document("workpane")["pluginFolders"], json::array({folder.path().generic_string(), missing.generic_string()}));

        product.emit(surface, *reader.nodeShowing("workpane.plugins.restart"), "click", json::object());
        product.frame();
        ASSERT_TRUE(product.answerDialog("confirm"));
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.product().restarting(); }));
        // clang-format on
    }

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().plugins().find("notes") != nullptr && product.product().shell().toasts().showing("A plugin could not be loaded", "The plugin folder \"" + missing.generic_string() + "\" could not be read"); }));
    // clang-format on
    EXPECT_EQ(product.product().plugins().find("notes")->directory, std::filesystem::weakly_canonical(folder.path() / "notes"));
    EXPECT_NE(product.product().plugins().find("logs")->directory, std::filesystem::weakly_canonical(folder.path() / "logs"));
    EXPECT_TRUE(product.product().shell().toasts().showing("A plugin could not be loaded", "The plugin \"logs\" in \"" + (folder.path() / "logs").generic_string() + "\" was not loaded, because a plugin with the same name was already loaded"));
    EXPECT_TRUE(linked || product.product().shell().toasts().showing("A plugin could not be loaded", "The plugin \"broken\" was not loaded, and its features are unavailable"));

    search(product, "plugin");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(surface) != nullptr; }));
    // clang-format on
    const SurfaceReader reader(product.declared(surface));
    EXPECT_FALSE(reader.properties(reader.nodes("alert").front()).value("visible", true));
    product.emit(surface, reader.nodes("table").back(), "action", {{"id", missing.generic_string()}, {"action", "remove"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(surface)).properties(reader.nodes("alert").front()).value("visible", false); }));
    // clang-format on
    EXPECT_EQ(product.product().preferences().document("workpane")["pluginFolders"], json::array({folder.path().generic_string()}));
}

// The general settings are found by the names of the languages and themes they offer, and both lists are sorted in the language of the reader.
TEST_F(ProductTest, FindsTheGeneralSettingsByTheLanguagesAndThemesTheyOffer) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    std::vector<std::string> keys;

    for (const auto& group : product.product().shell().settings().groups()) {
        for (const auto& section : group.sections) {
            if (group.owner == "workpane" && section.id == "general") {
                keys = section.searchKeys;
            }
        }
    }

    for (const std::string_view key : {"workpane.language.english", "workpane.language.portuguese", "workpane.application.theme-green", "workpane.application.theme-blue", "workpane.application.theme-red"}) {
        EXPECT_NE(std::ranges::find(keys, key), keys.end()) << key;
    }

    search(product, "Language");
    const std::string surface = "settings:workpane:application:general";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(surface) != nullptr; }));
    // clang-format on

    for (const ui::NodeId combo : SurfaceReader(product.declared(surface)).nodes("combo")) {
        EXPECT_TRUE(SurfaceReader(product.declared(surface)).properties(combo).value("sorted", false));
    }
}

// The window behind a dialog darkens over a few frames, so the product keeps drawing until it is fully dimmed and then sleeps again.
TEST_F(ProductTest, KeepsDrawingWhileADialogDimsTheWindowAndThenSleeps) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.demand().animating; }));
    // clang-format on

    product.product().requestQuit();
    product.frame();
    ASSERT_TRUE(product.product().shell().dialogs().active());
    EXPECT_TRUE(product.demand().animating);

    int frames = 1;

    while (product.demand().animating && frames < 60) {
        product.frame();
        ++frames;
    }

    EXPECT_GT(frames, 2);
    EXPECT_LT(frames, 60);
}

// A plugin reads the selected language and theme, tells a failure from any other value and asks to quit through the one confirmation of the product.
TEST_F(ProductTest, AnswersAPluginTheStateOfTheAppAndAsksBeforeItQuits) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    const auto loaded = product.product().runtime().loadString(R"(
        local bridge = require("workpane.bridge")
        local workpane = require("workpane.api").create("logs", "", { version = "", debug = false, platform = "", architecture = "", paths = { data = "" }, languages = {}, themes = {}, icons = {}, colors = {} })
        local seen = { workpane.app.language(), workpane.app.theme(), tostring(workpane.isFailure(bridge.failure({ code = "sample_refused" }))), tostring(workpane.isFailure({ code = "sample_refused" })) }
        bridge.call("workpane_log", { plugin = "logs", level = "info", category = "suite", message = "app:" .. table.concat(seen, ","), details = {} })
        workpane.app.quit()
    )",
                                                               "app");
    ASSERT_TRUE(loaded.hasValue()) << loaded.error().message;
    const std::string expected = "app:" + product.product().localization().language() + "," + std::string(product.product().render().theme().id()) + ",true,false";
    // clang-format off
    EXPECT_TRUE(product.frameUntil([&]() { return stored(data.path(), expected); }));
    // clang-format on
    EXPECT_TRUE(product.product().shell().dialogs().active());
    EXPECT_FALSE(product.product().quitting());
}

// Every destination of the mode bar and every settings category keeps an identity of its own, so hovering any of them finds no other item sharing it.
TEST_F(ProductTest, KeepsTheIdentitiesOfTheShellApart) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    ASSERT_TRUE(product.navigate(ui::Shell::settingsDestination).hasValue());
    product.frame();

    for (const float x : {30.0F, 180.0F}) {
        for (float y = 0.0F; y < HeadlessProduct::height; y += 8.0F) {
            product.moveTo(ImVec2(x, y));
            EXPECT_EQ(GImGui->DebugDrawIdConflictsId, 0U) << x << "," << y;
        }
    }
}

// A tooltip that appears asks for the frames ImGui needs to size it, so the product never sleeps on a tooltip cut to the size of its first frame.
TEST_F(ProductTest, KeepsDrawingUntilATooltipHasItsSize) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().shell().ready() && !product.demand().animating; }));
    const auto tooltip = []() { for (ImGuiWindow* window : GImGui->Windows) { if (window->Active && (window->Flags & ImGuiWindowFlags_Tooltip) != 0) { return window; } } return static_cast<ImGuiWindow*>(nullptr); };
    // clang-format on

    product.moveTo(ImVec2(30.0F, 40.0F));
    int frames = 0;

    while (tooltip() == nullptr && frames < 120) {
        product.frame();
        ++frames;
    }

    ASSERT_NE(tooltip(), nullptr);
    EXPECT_TRUE(product.demand().animating);

    while (product.demand().animating && frames < 240) {
        product.frame();
        ++frames;
    }

    ASSERT_NE(tooltip(), nullptr);
    EXPECT_FALSE(product.demand().animating);
    EXPECT_LE(tooltip()->ContentSize.x, tooltip()->Size.x);
    EXPECT_GT(tooltip()->Size.x, 0.0F);
}

TEST_F(ProductTest, StartsInTheLanguageOfTheSystem) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path(), "pt_BR.UTF-8");
    ASSERT_TRUE(product.boot().hasValue());
    EXPECT_EQ(product.product().localization().language(), "pt");
}

TEST_F(ProductTest, OpensADonationPageInTheDefaultBrowser) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    ASSERT_TRUE(product.navigate("donate:support").hasValue());
    const std::string surface = "view:donate:support";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(surface) != nullptr; }));
    // clang-format on
    const auto button = product.firstNode(surface, "button");
    ASSERT_TRUE(button.has_value());

    product.emit(surface, *button, "click", json::object());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.system().openedUrls.empty(); }));
    // clang-format on
    EXPECT_EQ(product.system().openedUrls.front(), "https://github.com/sponsors/paulocoutinhox");
}

TEST_F(ProductTest, KeepsTheCentralizedLogInTheLogsPlugin) {
    TemporaryDirectory data;
    {
        HeadlessProduct product(data.path());
        ASSERT_TRUE(product.boot().hasValue());
        product.product().logs().write(logging::LogLevel::Warning, "workpane", "suite", "Written by the suite", {{"step", 1}});
        ASSERT_TRUE(product.navigate("logs:viewer").hasValue());
        const std::string surface = "view:logs:viewer";
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() {
            const DeclaredSurface* mounted = product.declared(surface);
            const auto table = product.firstNode(surface, "table");
            return mounted != nullptr && table.has_value() && mounted->nodes.at(*table).properties["rows"].dump().find("Written by the suite") != std::string::npos;
        }));
        // clang-format on

        product.product().logs().write(logging::LogLevel::Info, "workpane", "suite", "Written just before closing");
    }

    EXPECT_TRUE(stored(data.path(), "Written by the suite"));
    EXPECT_TRUE(stored(data.path(), "Written just before closing"));
}

// The viewer adds new entries on top of the pages the reader loaded without reading them again, holds no more entries than those pages and offers the entries it let go through the older button.
TEST_F(ProductTest, KeepsTheOlderPagesOfTheLogViewerAsEntriesArrive) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());

    for (int entry = 0; entry < 250; ++entry) {
        product.product().logs().write(logging::LogLevel::Info, "workpane", "suite", "Entry " + std::to_string(entry));
    }

    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT sequence FROM logs__entries WHERE category = 'suite'").size() == 250U; }));
    // clang-format on
    ASSERT_TRUE(product.navigate("logs:viewer").hasValue());
    const std::string surface = "view:logs:viewer";
    // clang-format off
    const auto rows = [&]() { const DeclaredSurface* mounted = product.declared(surface); const auto table = product.firstNode(surface, "table"); return mounted != nullptr && table.has_value() ? mounted->nodes.at(*table).properties["rows"] : json::array(); };
    const auto older = [&]() { std::optional<ui::NodeId> found; for (const auto& [id, node] : product.declared(surface)->nodes) { if (node.kind == "button" && node.properties.dump().find("logs.viewer.load-older") != std::string::npos) { found = id; } } return found; };
    // clang-format on
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return rows().size() == 100U; }));
    // clang-format on
    ASSERT_TRUE(older().has_value());
    product.emit(surface, *older(), "click", json::object());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return rows().size() >= 200U; }));
    // clang-format on

    const std::string oldestShown = rows().back()["cells"][4]["text"];
    product.product().logs().write(logging::LogLevel::Info, "workpane", "suite", "Newest entry");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return rows().dump().find("Newest entry") != std::string::npos; }));
    // clang-format on

    EXPECT_EQ(rows().size(), 200U);
    EXPECT_EQ(rows().dump().find("\"" + oldestShown + "\""), std::string::npos);
    EXPECT_EQ(product.declared(surface)->nodes.at(*older()).properties["enabled"], true);
    product.emit(surface, *older(), "click", json::object());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return rows().dump().find("\"" + oldestShown + "\"") != std::string::npos; }));
    // clang-format on
    product.stop();
}

// The log keeps its newest fifty thousand entries, so a program that writes all day never fills the disk.
TEST_F(ProductTest, KeepsOnlyTheNewestEntriesOfTheLog) {
    TemporaryDirectory data;

    {
        HeadlessProduct product(data.path());
        ASSERT_TRUE(product.boot().hasValue());
        product.stop();
    }

    {
        auto database = tests::ProductDatabase::open(data);
        ASSERT_TRUE(database.run("WITH RECURSIVE counter(n) AS (SELECT 1 UNION ALL SELECT n + 1 FROM counter WHERE n < 50010) INSERT INTO logs__entries(timestamp_utc, source, level, category, message, details_json) SELECT '2026-01-01T00:00:00.000Z', 'workpane', 'info', 'seed', 'Seed ' || n, '{}' FROM counter").hasValue());
    }

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    product.product().logs().write(logging::LogLevel::Info, "workpane", "suite", "Newest entry");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.query("SELECT sequence FROM logs__entries WHERE message = 'Newest entry'").empty(); }));
    // clang-format on

    EXPECT_EQ(product.query("SELECT COUNT(*) AS kept FROM logs__entries").front()["kept"], 50000);
    EXPECT_TRUE(product.query("SELECT sequence FROM logs__entries WHERE message = 'Seed 1'").empty());
    EXPECT_FALSE(product.query("SELECT sequence FROM logs__entries WHERE message = 'Seed 50010'").empty());
    product.stop();
}

// Work that only writes the log and stores it reaches the plugins without asking for a frame, so a quiet window keeps sleeping.
TEST_F(ProductTest, DrawsNothingForWorkThatLeavesTheScreenAsItWas) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    product.settle(std::chrono::milliseconds(300));
    const auto quiet = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);

    while (std::chrono::steady_clock::now() < quiet) {
        std::ignore = product.product().service();
    }

    product.frame();
    product.product().logs().write(logging::LogLevel::Info, "workpane", "suite", "Quiet entry");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    bool changed = false;
    bool stored = false;

    while (!stored && std::chrono::steady_clock::now() < deadline) {
        changed = product.product().service() || changed;
        stored = !product.query("SELECT sequence FROM logs__entries WHERE message = 'Quiet entry'").empty();
    }

    EXPECT_TRUE(stored);
    EXPECT_FALSE(changed);
    product.stop();
}

// A log table an earlier build wrote refuses the Logs plugin at the start and tells the reader where the data is, instead of keeping entries it can never write.
TEST_F(ProductTest, RefusesTheLogsPluginOverTablesAnEarlierBuildWrote) {
    TemporaryDirectory data;

    {
        HeadlessProduct product(data.path());
        ASSERT_TRUE(product.boot().hasValue());
        product.stop();
    }

    {
        auto database = tests::ProductDatabase::open(data);
        ASSERT_TRUE(database.run("ALTER TABLE logs__entries DROP COLUMN details_json").hasValue());
    }

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    const std::string file = (data.path() / persistence::DatabaseBootstrap::databaseName).generic_string();
    // clang-format off
    EXPECT_TRUE(product.frameUntil([&]() { return product.product().shell().toasts().showing("A plugin could not be loaded", "The stored data of \"Logs\" could not be read, so it was not loaded. Its data is kept in \"" + file + "\""); }));
    // clang-format on
    EXPECT_FALSE(product.navigate("logs:viewer").hasValue());
    product.stop();
}

// Another plugin reads the log a page at a time and empties it on purpose, and a page it asks for badly is refused by its code.
TEST_F(ProductTest, OffersTheLogToOtherPlugins) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    const auto loaded = product.product().runtime().loadString(R"(
        local async = require("async")
        local bridge = require("workpane.bridge")
        local task = require("workpane.task")
        local workpane = require("workpane.api").create("browser", "", { version = "", debug = false, platform = "", architecture = "", paths = { data = "" }, languages = {}, themes = {}, icons = {}, colors = {} })

        task.run("browser", "suite", function()
            bridge.call("workpane_log", { plugin = "browser", level = "warning", category = "suite", message = "Kept for the reader", details = { place = "here" } })
            local found

            for _ = 1, 100 do
                local page = workpane.await(workpane.capabilities.request("logs.entries.page", { beforeSequence = 0, limit = 100 }))

                for _, entry in ipairs(page.entries) do
                    found = entry.message == "Kept for the reader" and entry or found
                end

                if found ~= nil then
                    break
                end

                async.sleep(20):await()
            end

            local _, refused = workpane.capabilities.request("logs.entries.page", { beforeSequence = 0, limit = 500 }):await()
            workpane.await(workpane.capabilities.request("logs.entries.clear", {}))
            local emptied = workpane.await(workpane.capabilities.request("logs.entries.page", { beforeSequence = 0, limit = 100 }))
            local report = "page:" .. found.source .. ":" .. found.level .. ":" .. found.details.place .. ":" .. refused.code .. ":" .. #emptied.entries
            bridge.call("workpane_log", { plugin = "workpane", level = "info", category = "suite", message = report, details = {} })
        end)
    )",
                                                               "logs");
    ASSERT_TRUE(loaded.hasValue()) << loaded.error().message;
    // clang-format off
    EXPECT_TRUE(product.frameUntil([&]() { return stored(data.path(), "page:browser:warning:here:logs_page_invalid:0"); }));
    // clang-format on
}

// The services are reached only in the name of a registered plugin, so a call naming anything else is refused before it does any work.
// Only a registered plugin can be removed, so neither the core nor a name nobody registered loses its catalog, and a path to reveal is absolute.
TEST_F(ProductTest, RefusesThePlatformServicesToAPluginThatIsNotRegistered) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    const auto loaded = product.product().runtime().loadString(R"(
        local bridge = require("workpane.bridge")
        local calls = {
            { "workpane_http_serve", { plugin = "nobody", request = 1, host = "127.0.0.1", port = 8080, root = "/" } },
            { "workpane_http_stop", { plugin = "nobody", server = 1 } },
            { "workpane_http_forget", { plugin = "nobody" } },
            { "workpane_system_information", { plugin = "nobody", request = 2 } },
            { "workpane_system_home", { plugin = "nobody" } },
            { "workpane_system_shell", { plugin = "nobody" } },
            { "workpane_process_start", { plugin = "nobody", program = "/bin/sh", arguments = {}, directory = "/", variables = {}, cleared = {} } },
            { "workpane_process_write", { plugin = "nobody", process = 1, text = "" } },
            { "workpane_process_stop", { plugin = "nobody", process = 1 } },
            { "workpane_process_forget", { plugin = "nobody" } },
            { "workpane_process_find", { plugin = "nobody", request = 4, name = "sh", directories = {} } },
            { "workpane_files_list", { plugin = "nobody", request = 5, path = "/" } },
            { "workpane_files_walk", { plugin = "nobody", request = 6, root = "/", maximum = 1, skip = {} } },
            { "workpane_files_search", { plugin = "nobody", request = 7, root = "/", text = "x", maximumMatches = 1, maximumFileBytes = 1, skip = {} } },
            { "workpane_files_canonical", { plugin = "nobody", request = 8, path = "/" } },
            { "workpane_files_access", { plugin = "nobody", request = 10, path = "/" } },
            { "workpane_database_query", { plugin = "nobody", request = 3, sql = "SELECT 1", bindings = {} } },
            { "workpane_ui_failed", { plugin = "nobody", surface = "view:nobody:main" } },
            { "workpane_dialog_buttons", { plugin = "nobody", surface = "dialog:nobody:1", buttons = {} } },
            { "workpane_dialog_close", { plugin = "nobody", surface = "dialog:nobody:1", button = "close" } },
            { "workpane_plugin_remove", { plugin = "nobody" } },
            { "workpane_plugin_remove", { plugin = "workpane" } },
            { "workpane_reveal_path", { plugin = "logs", request = 11, path = "relative/folder" } },
            { "workpane_database_query", { plugin = "logs", request = 12, sql = "SELECT ? AS value", bindings = { 0 / 0 } } },
        }

        local codes = {}

        for index, call in ipairs(calls) do
            local _, failure = pcall(bridge.call, call[1], call[2])
            codes[index] = call[1] .. "=" .. (failure ~= nil and failure.code or "accepted")
        end

        bridge.call("workpane_log", { plugin = "workpane", level = "info", category = "suite", message = table.concat(codes, " "), details = {} })
    )",
                                                               "refusals");
    ASSERT_TRUE(loaded.hasValue()) << loaded.error().message;

    const std::string expected = "workpane_http_serve=plugin_unknown workpane_http_stop=plugin_unknown workpane_http_forget=plugin_unknown workpane_system_information=plugin_unknown workpane_system_home=plugin_unknown workpane_system_shell=plugin_unknown workpane_process_start=plugin_unknown workpane_process_write=plugin_unknown workpane_process_stop=plugin_unknown workpane_process_forget=plugin_unknown workpane_process_find=plugin_unknown workpane_files_list=plugin_unknown workpane_files_walk=plugin_unknown workpane_files_search=plugin_unknown workpane_files_canonical=plugin_unknown workpane_files_access=plugin_unknown workpane_database_query=plugin_unknown workpane_ui_failed=plugin_unknown workpane_dialog_buttons=plugin_unknown workpane_dialog_close=plugin_unknown workpane_plugin_remove=plugin_unknown workpane_plugin_remove=plugin_unknown workpane_reveal_path=system_path_invalid workpane_database_query=database_binding_invalid";
    // clang-format off
    EXPECT_TRUE(product.frameUntil([&]() { return stored(data.path(), expected); }));
    // clang-format on
    EXPECT_EQ(product.system().inspections, 0);
    EXPECT_EQ(product.product().localization().translate("workpane.plugin.failed-title"), "A plugin could not be loaded");
}

// A server whose bind finishes after its plugin was withdrawn stops instead of serving files for a plugin that no longer runs.
TEST_F(ProductTest, StopsAServerThatStartedAfterItsPluginWasWithdrawn) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    const int port = LocalPort::free();
    std::filesystem::create_directories(folder.path() / "serving");
    std::ofstream(folder.path() / "serving" / "plugin.lua") << "return { id = \"serving\", titleKey = \"serving.plugin.title\", start = function()\n    workpane.http.serve({ host = \"127.0.0.1\", port = " << port << ", root = workpane.plugin.directory })\n    error(\"start failed\")\nend }";
    std::ofstream(folder.path() / "serving" / "translations.lua") << R"(return { en = { ["serving.plugin.title"] = "Serving" }, pt = { ["serving.plugin.title"] = "Servindo" } })";
    {
        auto database = tests::ProductDatabase::open(data);
        ASSERT_TRUE(persistence::PreferenceStore::store(database, "workpane", {{"pluginFolders", json::array({folder.path().generic_string()})}}).hasValue());
    }

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    product.settle(std::chrono::milliseconds(500));
    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(1);

    EXPECT_FALSE(client.Get("/plugin.lua"));
    product.stop();
}

// A dialog button names an icon of the catalog, and only the plugin a dialog belongs to may replace its buttons.
TEST_F(ProductTest, RefusesDialogButtonsItCannotDrawOrThatAnotherPluginOwns) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    const auto loaded = product.product().runtime().loadString(R"(
        local bridge = require("workpane.bridge")
        local calls = {
            { "open", "workpane_dialog_open", { plugin = "logs", request = 9, kind = "custom", title = "Title", surface = "dialog:logs:77", buttons = { { id = "save", text = "Save", icon = "nothing" } } } },
            { "foreign", "workpane_dialog_buttons", { plugin = "logs", surface = "dialog:ai:1", buttons = {} } },
            { "icon", "workpane_dialog_buttons", { plugin = "logs", surface = "dialog:logs:1", buttons = { { id = "save", text = "Save", icon = "nothing" } } } },
        }

        local codes = {}

        for index, call in ipairs(calls) do
            local _, failure = pcall(bridge.call, call[2], call[3])
            codes[index] = call[1] .. "=" .. (failure ~= nil and failure.code or "accepted")
        end

        bridge.call("workpane_log", { plugin = "workpane", level = "info", category = "suite", message = table.concat(codes, " "), details = {} })
    )",
                                                               "dialog-refusals");
    ASSERT_TRUE(loaded.hasValue()) << loaded.error().message;

    // clang-format off
    EXPECT_TRUE(product.frameUntil([&]() { return stored(data.path(), "open=ui_icon_unknown foreign=ui_surface_foreign icon=ui_icon_unknown"); }));
    // clang-format on
}

// A plugin runs a program through the SDK: it writes to it, reads both streams in order as UTF-8, learns how it ended and finds executables.
TEST_F(ProductTest, RunsProgramsForAPluginThroughTheSdk) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    const std::string program = RootedPath::of("opt/tools/language-server").generic_string();
    product.processes().executables["language-server"] = program;
    ASSERT_TRUE(product.boot().hasValue());
    const std::string script = R"(
        local bridge = require("workpane.bridge")
        local process = require("workpane.process")
        local task = require("workpane.task")

        local function note(message)
            bridge.call("workpane_log", { plugin = "logs", level = "info", category = "suite", message = message, details = {} })
        end

        local identity = process.start("logs", { program = PROGRAM, arguments = { "--stdio" }, directory = DIRECTORY, variables = { MODE = "test" }, cleared = { "SECRET" }, onOutput = function(chunks)
            for _, chunk in ipairs(chunks) do
                note(chunk.stream .. ":" .. chunk.text)
            end
        end, onExit = function(code)
            note("exit:" .. code)
        end })

        process.write("logs", identity, "ping")
        process.start("logs", { program = PROGRAM, directory = DIRECTORY, input = "" })
        note("relative:" .. tostring(pcall(process.start, "logs", { program = "relative", directory = DIRECTORY })))
        task.run("logs", "suite", function()
            note("found:" .. tostring(process.find("logs", "language-server"):await()))
            note("missing:" .. tostring(process.find("logs", "nothing"):await()))
        end)
    )";
    const auto loaded = product.product().runtime().loadString("DIRECTORY = " + nlohmann::json(data.path().generic_string()).dump() + "\nPROGRAM = " + nlohmann::json(program).dump() + script, "processes");
    ASSERT_TRUE(loaded.hasValue()) << loaded.error().message;

    ASSERT_EQ(product.processes().launches.size(), 2U);
    EXPECT_FALSE(product.processes().launches[0].input.has_value());
    EXPECT_EQ(product.processes().launches[1].input, std::optional<std::string>(""));
    EXPECT_EQ(product.processes().launches[0].program, std::filesystem::path(program));
    EXPECT_EQ(product.processes().launches[0].arguments, std::vector<std::string>{"--stdio"});
    EXPECT_EQ(product.processes().launches[0].variables, (std::vector<std::pair<std::string, std::string>>{{"MODE", "test"}}));
    EXPECT_EQ(product.processes().launches[0].cleared, std::vector<std::string>{"SECRET"});
    EXPECT_EQ(product.processes().inputs[0], "ping");

    // A character split between two writes arrives whole, and the end comes after the last output.
    product.processes().events[0].output(process::ProcessStream::Output, "hel");
    product.processes().events[0].output(process::ProcessStream::Error, "warning");
    product.processes().events[0].output(process::ProcessStream::Output, "lo \xC3");
    product.frame();
    product.processes().events[0].output(process::ProcessStream::Output, "\xA7");
    product.processes().ended[0] = true;
    product.processes().events[0].exited({4, false});

    for (const std::string& expected : std::vector<std::string>{"relative:false", "found:" + program, "missing:nil", "output:hel", "error:warning", "output:lo ", "output:\xC3\xA7", "exit:4"}) {
        // clang-format off
        EXPECT_TRUE(product.frameUntil([&]() { return stored(data.path(), expected); })) << expected;
        // clang-format on
    }
}

// A plugin lists a folder, walks it, searches it and resolves paths through the SDK, each answered after the work ran on a worker.
TEST_F(ProductTest, ReadsFoldersForAPluginThroughTheSdk) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    std::filesystem::create_directories(folder.path() / "src");
    std::filesystem::create_directories(folder.path() / ".git");
    std::ofstream(folder.path() / "src" / "main.lua") << "print('workpane')\n";
    std::ofstream(folder.path() / ".git" / "HEAD") << "workpane";
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    const std::string script = R"(
        local bridge = require("workpane.bridge")
        local task = require("workpane.task")
        local workpane = require("workpane.api").create("logs", FOLDER, { version = "", debug = false, platform = "", architecture = "", paths = { data = "" }, languages = {}, themes = {}, icons = {}, colors = {} })

        local function note(message)
            bridge.call("workpane_log", { plugin = "logs", level = "info", category = "suite", message = message, details = {} })
        end

        task.run("logs", "suite", function()
            local names = {}

            for _, entry in ipairs(workpane.await(workpane.files.list(FOLDER))) do
                names[#names + 1] = entry.name .. "=" .. entry.kind
            end

            table.sort(names)
            note("listed:" .. table.concat(names, ","))
            local walked = workpane.await(workpane.files.walk(FOLDER, { maximum = 10, skip = { ".git" } }))
            note("walked:" .. table.concat(walked.paths, ",") .. ":" .. tostring(walked.complete))
            local found = workpane.await(workpane.files.search(FOLDER, { text = "WORKPANE", maximumMatches = 10, maximumFileBytes = 1024, skip = { ".git" } }))
            note("found:" .. found.matches[1].path .. ":" .. found.matches[1].line .. ":" .. found.matches[1].text)
            note("canonical:" .. tostring(workpane.await(workpane.files.canonical(FOLDER .. "/src/..")) == CANONICAL))
            local _, failure = workpane.files.canonical(FOLDER .. "/missing"):await()
            note("missing:" .. failure.code)
            local _, refused = workpane.files.replace(FOLDER .. "/src/main.lua", FOLDER .. "/src"):await()
            note("refused:" .. refused.code)
            local skip = {}
            for index = 1, 65 do
                skip[index] = "folder" .. index
            end
            local _, skipping = workpane.files.walk(FOLDER, { maximum = 10, skip = skip }):await()
            note("skipping:" .. skipping.code)
            local _, long = workpane.files.search(FOLDER, { text = string.rep("x", 1001), maximumMatches = 10, maximumFileBytes = 1024, skip = {} }):await()
            note("long:" .. long.code)
        end)
    )";
    const std::string prelude = "FOLDER = " + nlohmann::json(folder.path().generic_string()).dump() + "\nCANONICAL = " + nlohmann::json(std::filesystem::canonical(folder.path()).generic_string()).dump() + "\n";
    const auto loaded = product.product().runtime().loadString(prelude + script, "folders");
    ASSERT_TRUE(loaded.hasValue()) << loaded.error().message;

    for (const std::string expected : {"listed:.git=directory,src=directory", "walked:src/main.lua:true", "found:src/main.lua:1:print('workpane')", "canonical:true", "missing:files_path_missing", "refused:files_replace_failed", "skipping:files_skip_too_long", "long:files_search_text_too_long"}) {
        // clang-format off
        EXPECT_TRUE(product.frameUntil([&]() { return stored(data.path(), expected); })) << expected;
        // clang-format on
    }
}

// A plugin reads the zone of the system and the offset a zone has at an instant, and a zone the system does not know is refused by its code.
TEST_F(ProductTest, AnswersTheTimeZonesOfTheSystemThroughTheSdk) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    product.system().timeZone = "Europe/Lisbon";
    product.system().zoneOffsets = {{"Europe/Lisbon", 3600}};
    ASSERT_TRUE(product.boot().hasValue());
    const std::string script = R"(
        local bridge = require("workpane.bridge")
        local task = require("workpane.task")
        local workpane = require("workpane.api").create("logs", "", { version = "", debug = false, platform = "", architecture = "", paths = { data = "" }, languages = {}, themes = {}, icons = {}, colors = {} })

        task.run("logs", "suite", function()
            local _, failure = pcall(workpane.time.offset, "Nowhere/Missing", 0)
            local message = "zone:" .. workpane.time.zone() .. ":" .. workpane.time.offset("Europe/Lisbon", 1774746000) .. ":" .. failure.code
            bridge.call("workpane_log", { plugin = "logs", level = "info", category = "suite", message = message, details = {} })
        end)
    )";
    const auto loaded = product.product().runtime().loadString(script, "zones");
    ASSERT_TRUE(loaded.hasValue()) << loaded.error().message;
    // clang-format off
    EXPECT_TRUE(product.frameUntil([&]() { return stored(data.path(), "zone:Europe/Lisbon:3600:time_zone_unknown"); }));
    // clang-format on
}

// A plugin reads the monospaced families of the machine by name in alphabetical order, listed once on a worker however often it asks.
TEST_F(ProductTest, OffersTheMonospacedFamiliesOfTheMachineThroughTheSdk) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    product.system().fonts = {{"Menlo", "/System/Library/Fonts/Menlo.ttc"}, {"Courier Prime", "/fonts/CourierPrime.ttf"}};
    ASSERT_TRUE(product.boot().hasValue());
    const std::string script = R"(
        local bridge = require("workpane.bridge")
        local task = require("workpane.task")
        local workpane = require("workpane.api").create("logs", "", { version = "", debug = false, platform = "", architecture = "", paths = { data = "" }, languages = {}, themes = {}, icons = {}, colors = {} })

        task.run("logs", "suite", function()
            local first = workpane.await(workpane.system.monospaceFonts())
            local second = workpane.await(workpane.system.monospaceFonts())
            local message = "fonts:" .. table.concat(first, ",") .. ":" .. table.concat(second, ",")
            bridge.call("workpane_log", { plugin = "logs", level = "info", category = "suite", message = message, details = {} })
        end)
    )";
    const auto loaded = product.product().runtime().loadString(script, "fonts");
    ASSERT_TRUE(loaded.hasValue()) << loaded.error().message;
    // clang-format off
    EXPECT_TRUE(product.frameUntil([&]() { return stored(data.path(), "fonts:Courier Prime,Menlo:Courier Prime,Menlo"); }));
    // clang-format on
}

// The text an input method composes is drawn over the caret of the focused field, and nothing is drawn when no field has the keyboard or the composition ended.
TEST_F(ProductTest, DrawsTheTextBeingComposedAtTheCaret) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    ASSERT_TRUE(product.navigate(ui::Shell::settingsDestination).hasValue());
    product.frame();
    // clang-format off
    const auto drawn = []() { return ImGui::GetForegroundDrawList()->VtxBuffer.Size; };
    // clang-format on
    const int unfocused = drawn();

    product.product().compose("´", 2);
    product.frame();
    EXPECT_EQ(drawn(), unfocused);

    product.press(HeadlessProduct::command() | ImGuiKey_F);
    product.frame();
    const int focused = drawn();
    ASSERT_TRUE(GImGui->PlatformImeData.WantVisible);
    EXPECT_GT(focused, unfocused);
    const ImVec2 caret = GImGui->PlatformImeData.InputPos;
    const ImDrawList& list = *ImGui::GetForegroundDrawList();
    // clang-format off
    EXPECT_TRUE(std::ranges::any_of(list.VtxBuffer, [&](const ImDrawVert& vertex) { return std::abs(vertex.pos.x - caret.x - 1.0F) < 2.0F; }));
    // clang-format on

    product.product().compose("", 0);
    product.frame();
    EXPECT_EQ(drawn(), unfocused);
}

// A plugin whose stop never ends holds the closing of the product only for its share of the time, so the plugins started before it still stop, and what their stops write still reaches the log, whose readers stop last.
TEST_F(ProductTest, StopsEveryPluginWhenOneStopNeverEnds) {
    TemporaryDirectory data;
    TemporaryDirectory folder;

    for (const std::string id : {"first", "hanging"}) {
        std::filesystem::create_directories(folder.path() / id);
        std::ofstream(folder.path() / id / "translations.lua") << "return { en = { [\"" + id + ".plugin.title\"] = \"" + id + "\" }, pt = { [\"" + id + ".plugin.title\"] = \"" + id + "\" } }";
    }

    std::ofstream(folder.path() / "first" / "plugin.lua") << R"(return { id = "first", titleKey = "first.plugin.title", start = function() end, stop = function() workpane.log.info("closing", "first stopped") end })";
    std::ofstream(folder.path() / "hanging" / "plugin.lua") << R"(return { id = "hanging", titleKey = "hanging.plugin.title", start = function() end, stop = function() local never = require("async").deferred() never:await() end })";
    {
        auto database = tests::ProductDatabase::open(data);
        ASSERT_TRUE(persistence::PreferenceStore::store(database, "workpane", {{"pluginFolders", json::array({folder.path().generic_string()})}}).hasValue());
    }

    {
        HeadlessProduct product(data.path());
        ASSERT_TRUE(product.boot().hasValue());
        product.settle(std::chrono::milliseconds(200));
        const auto closing = std::chrono::steady_clock::now();
        product.stop();
        EXPECT_LT(std::chrono::steady_clock::now() - closing, std::chrono::seconds(5));
    }

    EXPECT_TRUE(stored(data.path(), "first stopped"));
}

// A log subscriber that fails, or that writes to the log it reads, is removed after one error instead of feeding itself forever, and the product keeps drawing.
TEST_F(ProductTest, RemovesALogSubscriberThatFailsOrWritesToTheLog) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    std::filesystem::create_directories(folder.path() / "chatty");
    std::ofstream(folder.path() / "chatty" / "plugin.lua") << R"(return { id = "chatty", titleKey = "chatty.plugin.title", start = function()
    workpane.log.subscribe(function() error("broken subscriber") end)
    workpane.log.subscribe(function() workpane.log.info("loop", "echoed") end)
    workpane.log.info("test", "trigger")
end })";
    std::ofstream(folder.path() / "chatty" / "translations.lua") << R"(return { en = { ["chatty.plugin.title"] = "Chatty" }, pt = { ["chatty.plugin.title"] = "Tagarela" } })";
    {
        auto database = tests::ProductDatabase::open(data);
        ASSERT_TRUE(persistence::PreferenceStore::store(database, "workpane", {{"pluginFolders", json::array({folder.path().generic_string()})}}).hasValue());
    }

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    const std::string removed = "SELECT details_json FROM logs__entries WHERE source = 'chatty' AND message = 'A log subscriber failed and no longer receives entries'";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.query(removed).size() == 2U; }));
    // clang-format on
    product.settle(std::chrono::milliseconds(300));
    const auto failures = product.query(removed);

    ASSERT_EQ(failures.size(), 2U);
    EXPECT_NE((failures[0]["details_json"].get<std::string>() + failures[1]["details_json"].get<std::string>()).find("broken subscriber"), std::string::npos);
    EXPECT_NE((failures[0]["details_json"].get<std::string>() + failures[1]["details_json"].get<std::string>()).find("log_write_in_delivery"), std::string::npos);
    EXPECT_TRUE(product.query("SELECT message FROM logs__entries WHERE message = 'echoed'").empty());
    product.stop();
}

// Every log subscriber receives its own copy of each entry, so a plugin that rewrites what it receives never changes what the Logs plugin stores.
TEST_F(ProductTest, HandsEveryLogSubscriberItsOwnEntry) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    std::filesystem::create_directories(folder.path() / "meddler");
    std::ofstream(folder.path() / "meddler" / "plugin.lua") << R"(return { id = "meddler", titleKey = "meddler.plugin.title", start = function()
    workpane.log.subscribe(function(entry) entry.message = "rewritten" entry.category = "rewritten" end)
    workpane.log.info("test", "original")
end })";
    std::ofstream(folder.path() / "meddler" / "translations.lua") << R"(return { en = { ["meddler.plugin.title"] = "Meddler" }, pt = { ["meddler.plugin.title"] = "Intrometido" } })";
    {
        auto database = tests::ProductDatabase::open(data);
        ASSERT_TRUE(persistence::PreferenceStore::store(database, "workpane", {{"pluginFolders", json::array({folder.path().generic_string()})}}).hasValue());
    }

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.query("SELECT sequence FROM logs__entries WHERE source = 'meddler' AND message = 'original'").empty(); }));
    // clang-format on
    product.settle(std::chrono::milliseconds(300));

    EXPECT_TRUE(product.query("SELECT sequence FROM logs__entries WHERE message = 'rewritten' OR category = 'rewritten'").empty());
    product.stop();
}

// An added plugin that acts while it loads, takes the name of the core, breaks its definition or fails to start is refused alone, and nothing it did outlives the refusal.
// A plugin that rewrites the owner of its settings store still writes only its own document.
TEST_F(ProductTest, RefusesAnAddedPluginThatMisbehavesWithoutHarmingTheOthers) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    // clang-format off
    const auto install = [&folder](const std::string& id, const std::string& definition) {
        std::filesystem::create_directories(folder.path() / id);
        std::ofstream(folder.path() / id / "plugin.lua") << definition;
        std::ofstream(folder.path() / id / "translations.lua") << "return { en = { [\"" << id << ".plugin.title\"] = \"Title\" }, pt = { [\"" << id << ".plugin.title\"] = \"Titulo\" } }";
    };
    // clang-format on

    install("squatter", R"(workpane.capabilities.provide("logs.entries.page", function() return {} end) return { id = "squatter", titleKey = "squatter.plugin.title" })");
    install("workpane", R"(return { id = "workpane", titleKey = "workpane.plugin.title" })");
    install("failing", R"(return { id = "failing", titleKey = "failing.plugin.title", enabled = function() error("enabled failed") end })");
    install("tangled", R"(return { id = "tangled", titleKey = "tangled.plugin.title", dependencies = "logs" })");
    install("shy", R"(return { id = "shy", titleKey = "shy.plugin.title", offByDefault = "yes" })");
    install("quitter", R"(return { id = "quitter", titleKey = "quitter.plugin.title", start = function()
    workpane.task(function()
        require("async").sleep(50):await()
        workpane.capabilities.provide("quitter.late.answer", function() return {} end)
    end)
    error("start failed")
end })");
    install("forger", R"(return { id = "forger", titleKey = "forger.plugin.title", start = function()
    local store = workpane.preferences.define({ pluginFolders = { type = "list", items = { type = "string" } } })
    store.owner = "workpane"
    store:set("pluginFolders", { "forged" })
end })");
    install("observer", R"(return { id = "observer", titleKey = "observer.plugin.title", start = function()
    workpane.task(function()
        require("async").sleep(300):await()
        workpane.log.info("check", "late " .. tostring(workpane.capabilities.available("quitter.late.answer")))
    end)
end })");

    {
        auto database = tests::ProductDatabase::open(data);
        ASSERT_TRUE(persistence::PreferenceStore::store(database, "workpane", {{"pluginFolders", json::array({folder.path().generic_string()})}}).hasValue());
    }

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    const std::string late = "SELECT message FROM logs__entries WHERE source = 'observer' AND category = 'check'";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.query(late).empty(); }));
    // clang-format on

    const auto refusals = product.query("SELECT details_json FROM logs__entries WHERE message = 'A plugin could not be loaded'");
    // clang-format off
    const auto refused = [&refusals](std::string_view id, std::string_view code) {
        return std::ranges::any_of(refusals, [&](const json& row) { const json details = json::parse(row["details_json"].get<std::string>()); return details["plugin"] == json(id) && details["code"] == json(code); });
    };
    // clang-format on

    EXPECT_TRUE(refused("squatter", "plugin_not_running"));
    EXPECT_TRUE(refused("workpane", "plugin_identifier_invalid"));
    EXPECT_TRUE(refused("failing", "lua_error"));
    EXPECT_TRUE(refused("tangled", "plugin_definition_invalid"));
    EXPECT_TRUE(refused("shy", "plugin_definition_invalid"));
    EXPECT_TRUE(refused("quitter", "lua_error"));
    EXPECT_EQ(product.query(late)[0]["message"], "late false");

    std::set<std::string> destinations;

    for (const auto& item : product.product().shell().navigation()) {
        destinations.insert(item.destination());
    }

    EXPECT_TRUE(destinations.contains("logs:viewer"));
    EXPECT_TRUE(destinations.contains("system-information:overview"));
    EXPECT_EQ(product.product().preferences().document("workpane")["pluginFolders"], json::array({folder.path().generic_string()}));
    EXPECT_EQ(product.product().preferences().document("forger")["pluginFolders"], json::array({"forged"}));
    product.stop();
}

// The reader switches a plugin off and on while the product runs: its dependents stop after a confirmation, its destinations and band leave and come back, the plugins watching its capability hear it go and return, the choice is kept, and its data can be erased while it does not run.
TEST_F(ProductTest, SwitchesPluginsFromTheSettingsWhileTheProductRuns) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    const std::string surface = "settings:workpane:plugins:installed";
    // clang-format off
    const auto install = [&folder](const std::string& id, const std::string& definition) {
        std::filesystem::create_directories(folder.path() / id);
        std::ofstream(folder.path() / id / "plugin.lua") << definition;
        std::ofstream(folder.path() / id / "translations.lua") << "return { en = { [\"" << id << ".plugin.title\"] = \"" << id << "\", [\"" << id << ".band.title\"] = \"Strip\" }, pt = { [\"" << id << ".plugin.title\"] = \"" << id << "\", [\"" << id << ".band.title\"] = \"Faixa\" } }";
    };
    // clang-format on

    install("base", R"(return { id = "base", titleKey = "base.plugin.title",
        navigation = { { id = "main", titleKey = "base.plugin.title", icon = "file", placement = "primary", order = 950, view = function() return workpane.ui.label({ text = "base" }) end } },
        bands = { { id = "strip", titleKey = "base.band.title", placement = "bottom", order = 900, height = 40, view = function() return workpane.ui.label({ text = "strip" }) end } },
        start = function()
            local store = workpane.preferences.define({ count = { type = "integer", default = 0 } })
            workpane.log.info("count", tostring(store:get("count")))
            store:set("count", 3)
            workpane.capabilities.provide("base.data.read", function() return {} end)
            workpane.task(function()
                require("async").sleep(100):await()
                workpane.shell.resizeBand("strip", 64)
            end)
        end })");
    install("addon", R"(return { id = "addon", titleKey = "addon.plugin.title", dependencies = { "base" },
        navigation = { { id = "main", titleKey = "addon.plugin.title", icon = "file", placement = "primary", order = 951, view = function() return workpane.ui.label({ text = "addon" }) end } } })");
    install("observer", R"(return { id = "observer", titleKey = "observer.plugin.title", start = function()
        workpane.capabilities.watch("base.data.read", function(available) workpane.log.info("watch", tostring(available)) end)
    end })");

    {
        auto database = tests::ProductDatabase::open(data);
        ASSERT_TRUE(persistence::PreferenceStore::store(database, "workpane", {{"pluginFolders", json::array({folder.path().generic_string()})}}).hasValue());
        ASSERT_TRUE(persistence::PreferenceStore::store(database, "gone", {{"left", 1}}).hasValue());
    }

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    // clang-format off
    const auto destinations = [&product]() {
        std::set<std::string> found;

        for (const auto& item : product.product().shell().navigation()) {
            found.insert(item.destination());
        }

        return found;
    };

    const auto bandHeight = [&product]() {
        for (const auto& band : product.product().shell().bands()) {
            if (band.plugin == "base") {
                return band.height;
            }
        }

        return std::int64_t{-1};
    };

    const auto rows = [&](std::string_view id) {
        const SurfaceReader reader(product.declared(surface));
        const auto list = reader.nodes("settingsRow");
        const auto toggles = reader.nodes("toggle");

        for (std::size_t index = 0; index < list.size(); ++index) {
            if (reader.properties(list[index])["label"].value("key", "") == std::string(id) + ".plugin.title") {
                return std::optional<ui::NodeId>(toggles[index]);
            }
        }

        return std::optional<ui::NodeId>();
    };

    const auto eraser = [&](std::string_view id) {
        const SurfaceReader reader(product.declared(surface));
        const auto list = reader.nodes("settingsRow");
        const auto buttons = reader.nodes("button");

        for (std::size_t index = 0; index < list.size(); ++index) {
            if (reader.properties(list[index])["label"].value("key", "") == std::string(id) + ".plugin.title") {
                return std::optional<ui::NodeId>(buttons[index]);
            }
        }

        return std::optional<ui::NodeId>();
    };

    const auto erasable = [&](std::string_view id) { return SurfaceReader(product.declared(surface)).properties(*eraser(id)).value("visible", true); };

    ASSERT_TRUE(product.frameUntil([&]() { return product.surface("band:base:strip") != nullptr && bandHeight() == 64; }));
    // clang-format on
    ASSERT_TRUE(product.navigate("addon:main").hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface("view:addon:main") != nullptr; }));
    // clang-format on
    product.frame();

    // The band stands across the bottom of the window and the view ends above it.
    const ImGuiWindow* band = nullptr;
    const ImGuiWindow* view = nullptr;

    for (const ImGuiWindow* window : GImGui->Windows) {
        const std::string_view name(window->Name);
        band = window->Active && name.find("band:base:strip") != std::string_view::npos ? window : band;
        view = window->Active && name.find("view:addon:main") != std::string_view::npos ? window : view;
    }

    ASSERT_NE(band, nullptr);
    ASSERT_NE(view, nullptr);
    EXPECT_FLOAT_EQ(band->Pos.x, 0.0F);
    EXPECT_FLOAT_EQ(band->Pos.y + band->Size.y, ImGui::GetIO().DisplaySize.y);
    EXPECT_LE(view->Pos.y + view->Size.y, band->Pos.y);

    search(product, "plugin");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(surface) != nullptr && rows("base").has_value(); }));
    // clang-format on

    // Each row names the folder its plugin was read from in its tooltip.
    const SurfaceReader listed(product.declared(surface));
    const auto settingsRows = listed.nodes("settingsRow");
    // clang-format off
    const auto described = std::ranges::find_if(settingsRows, [&](ui::NodeId row) { return listed.properties(row)["label"].value("key", "") == "base.plugin.title"; });
    // clang-format on
    ASSERT_NE(described, settingsRows.end());
    EXPECT_EQ(std::filesystem::path(listed.properties(*described)["tooltip"].get<std::string>()), folder.path() / "base");

    // A running plugin never offers to erase the data it is using.
    product.settle(std::chrono::milliseconds(150));
    EXPECT_FALSE(erasable("base"));

    product.emit(surface, *rows("base"), "change", {{"checked", false}});
    product.frame();
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !destinations().contains("base:main") && !destinations().contains("addon:main") && bandHeight() == -1; }));
    // clang-format on
    EXPECT_EQ(product.surface("band:base:strip"), nullptr);
    EXPECT_EQ(product.product().preferences().document("workpane")["pluginSwitches"], json({{"base", false}}));
    EXPECT_EQ(product.product().preferences().document("base")["count"], 3);

    // The plugin turned off offers to erase the data it keeps beside its switch, and erasing it leaves nothing of it in the database.
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return erasable("base"); }));
    // clang-format on
    EXPECT_FALSE(erasable("addon"));
    EXPECT_FALSE(erasable("observer"));
    product.emit(surface, *eraser("base"), "click", json::object());
    product.frame();
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().preferences().document("base").empty() && !erasable("base"); }));
    // clang-format on

    // A plugin that is gone but left data is listed among the removed plugins until its data is erased, and the block hides again.
    const SurfaceReader installed(product.declared(surface));
    const ui::NodeId removed = installed.nodes("table").front();
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(surface)).properties(removed)["rows"].size() == 1U; }));
    // clang-format on
    EXPECT_EQ(installed.properties(removed)["rows"][0]["id"], "gone");
    EXPECT_TRUE(installed.properties(installed.nodes("sectionTitle").front()).value("visible", false));
    product.emit(surface, removed, "action", {{"id", "gone"}, {"action", "erase"}});
    product.frame();
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(surface)).properties(removed)["rows"].empty() && !SurfaceReader(product.declared(surface)).properties(removed).value("visible", true); }));
    // clang-format on
    EXPECT_TRUE(product.product().preferences().document("gone").empty());

    product.emit(surface, *rows("base"), "change", {{"checked", true}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return destinations().contains("base:main") && destinations().contains("addon:main") && product.surface("band:base:strip") != nullptr; }));
    // clang-format on
    EXPECT_EQ(product.product().preferences().document("workpane")["pluginSwitches"], json({{"base", true}}));

    const std::string watching = "SELECT message FROM logs__entries WHERE source = 'observer' AND category = 'watch' ORDER BY sequence";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.query(watching).size() >= 2U; })) << json(product.query("SELECT source, category, message FROM logs__entries WHERE level = 'error'")).dump();
    // clang-format on
    const auto watched = product.query(watching);
    EXPECT_EQ(watched[watched.size() - 2]["message"], "false");
    EXPECT_EQ(watched.back()["message"], "true");
    const std::string counting = "SELECT message FROM logs__entries WHERE source = 'base' AND category = 'count' ORDER BY sequence";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.query(counting).size() == 2U; }));
    // clang-format on
    const auto counts = product.query(counting);
    EXPECT_EQ(counts[1]["message"], "0");
    product.stop();
}

// A plugin hears the language and the theme the reader chooses, and the dialogs of a plugin the reader switches off close with it.
TEST_F(ProductTest, TellsPluginsTheChoicesOfTheReaderAndClosesTheDialogsOfAStoppedPlugin) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    std::filesystem::create_directories(folder.path() / "asker");
    std::ofstream(folder.path() / "asker" / "translations.lua") << R"(return { en = { ["asker.plugin.title"] = "Asker" }, pt = { ["asker.plugin.title"] = "Perguntador" } })";
    std::ofstream(folder.path() / "asker" / "plugin.lua") << R"(return { id = "asker", titleKey = "asker.plugin.title", start = function()
        workpane.app.watch(function() workpane.dialogs.confirm({ title = "Asked" }):await() end)
    end })";
    std::filesystem::create_directories(folder.path() / "hearer");
    std::ofstream(folder.path() / "hearer" / "translations.lua") << R"(return { en = { ["hearer.plugin.title"] = "Hearer" }, pt = { ["hearer.plugin.title"] = "Ouvinte" } })";
    std::ofstream(folder.path() / "hearer" / "plugin.lua") << R"(return { id = "hearer", titleKey = "hearer.plugin.title", start = function()
        workpane.app.watch(function(choices) workpane.log.info("reader", choices.language .. " " .. choices.theme) end)
    end })";

    {
        auto database = tests::ProductDatabase::open(data);
        ASSERT_TRUE(persistence::PreferenceStore::store(database, "workpane", {{"pluginFolders", json::array({folder.path().generic_string()})}, {"language", "en"}}).hasValue());
    }

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    const std::string general = "settings:workpane:application:general";
    const std::string plugins = "settings:workpane:plugins:installed";
    search(product, "Language");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(general) != nullptr; }));
    // clang-format on
    search(product, "plugin");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(plugins) != nullptr; }));
    // clang-format on

    // A change of language reaches every plugin that follows the reader, and the one that asks opens its dialog.
    product.emit(general, SurfaceReader(product.declared(general)).nodes("combo").front(), "change", {{"value", "pt"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().shell().dialogs().active(); }));
    // clang-format on
    const SurfaceReader reader(product.declared(plugins));
    const auto rows = reader.nodes("settingsRow");
    const auto toggles = reader.nodes("toggle");

    for (std::size_t index = 0; index < rows.size(); ++index) {
        if (reader.properties(rows[index])["label"].value("key", "") == "asker.plugin.title") {
            product.emit(plugins, toggles[index], "change", {{"checked", false}});
        }
    }

    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.product().shell().dialogs().active() && product.product().plugins().find("asker") == nullptr; }));
    // clang-format on
    const std::string heard = "SELECT message FROM logs__entries WHERE source = 'hearer' AND category = 'reader'";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.query(heard).empty(); }));
    // clang-format on
    EXPECT_EQ(product.query(heard)[0]["message"].get<std::string>().substr(0, 3), "pt ");
    product.stop();
}

// A plugin plays the sound files of its assets and stops them, every refused sound names its code, and the sounds of a plugin the reader switches off stop with it.
TEST_F(ProductTest, PlaysTheSoundsOfAPluginAndStopsThemWithIt) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    std::filesystem::create_directories(folder.path() / "noisy" / "assets" / "sounds");
    tests::WaveFile::write(folder.path() / "noisy" / "assets" / "sounds" / "beep.wav", 0.05);
    std::ofstream(folder.path() / "noisy" / "assets" / "notes.txt") << "not a sound";
    std::ofstream(folder.path() / "noisy" / "translations.lua") << R"(return { en = { ["noisy.plugin.title"] = "Noisy" }, pt = { ["noisy.plugin.title"] = "Barulhento" } })";
    std::ofstream(folder.path() / "noisy" / "plugin.lua") << R"(return { id = "noisy", titleKey = "noisy.plugin.title", start = function()
        local codes = {}
        local function attempt(path, options)
            local played, failure = pcall(workpane.audio.play, path, options)
            codes[#codes + 1] = played and "played" or failure.code
        end
        local once = workpane.audio.play("sounds/beep.wav", { volume = 0.25 })
        workpane.audio.play("sounds/beep.wav", { loop = true })
        attempt("../plugin.lua")
        attempt("notes.txt")
        attempt("sounds/missing.wav")
        attempt("sounds/beep.wav", { volume = 2 })
        workpane.audio.stop(once)
        local _, unknown = pcall(workpane.audio.stop, once)
        codes[#codes + 1] = unknown.code
        workpane.log.info("audio", table.concat(codes, ","))
    end })";

    {
        auto database = tests::ProductDatabase::open(data);
        ASSERT_TRUE(persistence::PreferenceStore::store(database, "workpane", {{"pluginFolders", json::array({folder.path().generic_string()})}}).hasValue());
    }

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    const std::string heard = "SELECT message FROM logs__entries WHERE source = 'noisy' AND category = 'audio'";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.query(heard).empty(); }));
    // clang-format on
    EXPECT_EQ(product.query(heard)[0]["message"], "audio_path_invalid,audio_format_unsupported,audio_file_missing,audio_volume_invalid,audio_sound_unknown");
    ASSERT_EQ(product.audio().played.size(), 2U);
    EXPECT_EQ(product.audio().played[0].file.filename(), "beep.wav");
    EXPECT_FLOAT_EQ(product.audio().played[0].volume, 0.25F);
    EXPECT_FALSE(product.audio().played[0].loop);
    EXPECT_TRUE(product.audio().played[1].loop);
    EXPECT_EQ(product.audio().stopped, (std::vector<std::uint64_t>{1}));

    const std::string plugins = "settings:workpane:plugins:installed";
    search(product, "plugin");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(plugins) != nullptr; }));
    // clang-format on
    const SurfaceReader reader(product.declared(plugins));
    const auto rows = reader.nodes("settingsRow");

    for (std::size_t index = 0; index < rows.size(); ++index) {
        if (reader.properties(rows[index])["label"].value("key", "") == "noisy.plugin.title") {
            product.emit(plugins, reader.nodes("toggle")[index], "change", {{"checked", false}});
        }
    }

    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.audio().stopped == std::vector<std::uint64_t>{1, 2}; }));
    // clang-format on
    product.stop();
}

// Every settings group is listed alphabetically in the language of the reader, the groups of the core among them, so the settings open on the first group of that order.
TEST_F(ProductTest, ListsEverySettingsGroupAlphabetically) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    ASSERT_TRUE(product.navigate(ui::Shell::settingsDestination).hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface("settings:ai:agents:general") != nullptr; }));
    // clang-format on
    product.settle(std::chrono::milliseconds(100));
    EXPECT_EQ(product.surface("settings:workpane:application:general"), nullptr);
    EXPECT_EQ(product.surface("settings:workpane:plugins:installed"), nullptr);
    product.stop();
}

// Every frame of the product tessellates curves and smooths edges in pixels of the framebuffer it is drawn for.
TEST_F(ProductTest, DrawsItsShapesAtTheDensityOfTheFramebuffer) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    ImGui::GetIO().DisplayFramebufferScale = ImVec2(2.0F, 2.0F);
    product.frame();
    EXPECT_FLOAT_EQ(GImGui->DrawListSharedData.InitialFringeScale, 0.5F);
    EXPECT_FLOAT_EQ(GImGui->DrawListSharedData.CircleTessellationMaxError, 0.1F);
    ImGui::GetIO().DisplayFramebufferScale = ImVec2(1.0F, 1.0F);
    product.frame();
    EXPECT_FLOAT_EQ(GImGui->DrawListSharedData.InitialFringeScale, 1.0F);
    product.stop();
}

// A picture keeps its pixels only until the renderer created its texture, and a picture that failed keeps none.
TEST_F(ProductTest, KeepsEachPictureOnlyAsItsTexture) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    ui::TextureCache& textures = product.product().render().textures();
    const ui::Texture& picture = textures.request(Resources::staged() / "plugins" / "flappy-bird" / "assets" / "sprites" / "base.png");
    const ui::Texture& missing = textures.request(data.path() / "missing.png");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return picture.state == ui::TextureState::Ready && missing.state == ui::TextureState::Failed; }));
    // clang-format on
    ASSERT_NE(picture.data, nullptr);
    ASSERT_NE(picture.data->Pixels, nullptr);

    product.frame();
    product.frame();
    EXPECT_EQ(picture.data->Status, ImTextureStatus_OK);
    EXPECT_EQ(picture.data->Pixels, nullptr);
    EXPECT_EQ(picture.width, 336);
    EXPECT_EQ(missing.data, nullptr);
    product.stop();
}

// Input that arrives at once is spread by ImGui over several frames, and the product asks for each of those frames until the queue is drawn, so the last keys of a fast typist never wait for another event.
TEST_F(ProductTest, AsksForFramesUntilTheQueuedInputIsDrawn) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    product.settle(std::chrono::milliseconds(100));
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(400.0F, 300.0F);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    io.AddInputCharactersUTF8("abc");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    product.frame();
    EXPECT_TRUE(product.demand().input);
    int frames = 1;

    while (product.demand().input && frames < 20) {
        product.frame();
        ++frames;
    }

    EXPECT_FALSE(product.demand().input);
    EXPECT_GT(frames, 2);
    product.stop();
}

// A band whose view fails keeps its room at the bottom of the window, writes the reason once and is never asked for again while its plugin runs.
TEST_F(ProductTest, KeepsTheRoomOfABandWhoseViewFailed) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    std::filesystem::create_directories(folder.path() / "broken");
    std::ofstream(folder.path() / "broken" / "translations.lua") << R"(return { en = { ["broken.plugin.title"] = "Broken", ["broken.band.title"] = "Strip" }, pt = { ["broken.plugin.title"] = "Quebrado", ["broken.band.title"] = "Faixa" } })";
    std::ofstream(folder.path() / "broken" / "plugin.lua") << R"(return { id = "broken", titleKey = "broken.plugin.title",
        bands = { { id = "strip", titleKey = "broken.band.title", placement = "bottom", order = 900, height = 40, view = function() error("the band could not be drawn") end } } })";

    {
        auto database = tests::ProductDatabase::open(data);
        ASSERT_TRUE(persistence::PreferenceStore::store(database, "workpane", {{"pluginFolders", json::array({folder.path().generic_string()})}}).hasValue());
    }

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    const std::string failures = "SELECT details_json FROM logs__entries WHERE source = 'broken' AND message = 'A surface could not be built'";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.query(failures).empty(); }));
    // clang-format on
    product.settle(std::chrono::milliseconds(200));

    const auto told = product.query(failures);
    ASSERT_EQ(told.size(), 1U);
    EXPECT_EQ(json::parse(told[0]["details_json"].get<std::string>())["surface"], "band:broken:strip");
    EXPECT_EQ(product.surface("band:broken:strip"), nullptr);
    // clang-format off
    const auto band = std::ranges::find_if(product.product().shell().bands(), [](const ui::BandItem& item) { return item.plugin == "broken"; });
    // clang-format on
    ASSERT_NE(band, product.product().shell().bands().end());
    EXPECT_EQ(band->height, 40);
    product.stop();
}

// A machine that cannot play a sound still answers every call of a plugin, tells once that it has no audio device and tells a plugin in its own log that a file of its could not be opened.
TEST_F(ProductTest, AcceptsTheSoundsOfAPluginOnAMachineThatCannotPlayThem) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    std::filesystem::create_directories(folder.path() / "noisy" / "assets");
    tests::WaveFile::write(folder.path() / "noisy" / "assets" / "beep.wav", 0.05);
    tests::WaveFile::write(folder.path() / "noisy" / "assets" / "broken.wav", 0.05);
    std::ofstream(folder.path() / "noisy" / "translations.lua") << R"(return { en = { ["noisy.plugin.title"] = "Noisy" }, pt = { ["noisy.plugin.title"] = "Barulhento" } })";
    std::ofstream(folder.path() / "noisy" / "plugin.lua") << R"(return { id = "noisy", titleKey = "noisy.plugin.title", start = function()
        for _ = 1, 3 do
            workpane.audio.play("beep.wav")
        end
        workpane.audio.play("broken.wav")
        workpane.log.info("audio", "played")
    end })";

    {
        auto database = tests::ProductDatabase::open(data);
        ASSERT_TRUE(persistence::PreferenceStore::store(database, "workpane", {{"pluginFolders", json::array({folder.path().generic_string()})}}).hasValue());
    }

    HeadlessProduct product(data.path());
    product.audio().failures.emplace("beep.wav", Error{"audio_unavailable", "The machine has no audio device the product could open", "beep.wav"});
    product.audio().failures.emplace("broken.wav", Error{"audio_file_unreadable", "A sound file could not be opened", "broken.wav"});
    ASSERT_TRUE(product.boot().hasValue());
    const std::string warnings = "SELECT source, details_json FROM logs__entries WHERE category = 'audio' AND level = 'warning' ORDER BY sequence";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.query("SELECT 1 FROM logs__entries WHERE source = 'noisy' AND message = 'played'").empty() && product.query(warnings).size() >= 2; }));
    // clang-format on
    product.settle(std::chrono::milliseconds(100));

    const auto told = product.query(warnings);
    ASSERT_EQ(told.size(), 2U);
    EXPECT_EQ(told[0]["source"], "workpane");
    EXPECT_EQ(json::parse(told[0]["details_json"].get<std::string>())["code"], "audio_unavailable");
    EXPECT_EQ(told[1]["source"], "noisy");
    EXPECT_EQ(json::parse(told[1]["details_json"].get<std::string>())["code"], "audio_file_unreadable");
    EXPECT_EQ(product.audio().played.size(), 4U);
    product.stop();
}

// A finalizer of a plugin that runs while the product closes meets a closed bridge instead of host functions that were already released.
TEST_F(ProductTest, ClosesTheBridgeBeforeTheFinalizersOfThePluginsRun) {
    TemporaryDirectory data;
    TemporaryDirectory folder;
    std::filesystem::create_directories(folder.path() / "lingering");
    std::ofstream(folder.path() / "lingering" / "plugin.lua") << R"(local kept = setmetatable({}, { __gc = function() workpane.log.info("gc", "finalized") end })
return { id = "lingering", titleKey = "lingering.plugin.title", start = function() kept.started = true end })";
    std::ofstream(folder.path() / "lingering" / "translations.lua") << R"(return { en = { ["lingering.plugin.title"] = "Lingering" }, pt = { ["lingering.plugin.title"] = "Persistente" } })";
    {
        auto database = tests::ProductDatabase::open(data);
        ASSERT_TRUE(persistence::PreferenceStore::store(database, "workpane", {{"pluginFolders", json::array({folder.path().generic_string()})}}).hasValue());
    }

    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    product.settle(std::chrono::milliseconds(100));
    product.stop();

    EXPECT_FALSE(stored(data.path(), "finalized"));
    EXPECT_TRUE(storedErrors(data.path()).empty()) << json(storedErrors(data.path())).dump();
}

#if !defined(_WIN32)
// The product ignores a broken pipe from its start, so a peer that goes away in the middle of a write never ends it.
TEST_F(ProductTest, IgnoresBrokenPipesFromItsStart) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    struct sigaction current{};
    ASSERT_EQ(::sigaction(SIGPIPE, nullptr, &current), 0);

    EXPECT_EQ(current.sa_handler, SIG_IGN);
    product.stop();
}
#endif

// A start whose scripts could not load quits at once, since no bootstrap ran to answer the shutdown it would otherwise wait for.
TEST_F(ProductTest, QuitsAtOnceWhenItsScriptsCouldNotStart) {
    TemporaryDirectory data;
    TemporaryDirectory resources;
    std::filesystem::copy(Resources::staged(), resources.path(), std::filesystem::copy_options::recursive);
    std::ofstream(resources.path() / "lua" / "workpane" / "bootstrap.lua", std::ios::binary | std::ios::trunc) << "this is not lua";
    HeadlessProduct product(data.path(), "en_US", resources.path());
    EXPECT_FALSE(product.boot().hasValue());
    const auto stopping = std::chrono::steady_clock::now();
    product.stop();

    EXPECT_LT(std::chrono::steady_clock::now() - stopping, std::chrono::seconds(1));
}

// A start after the previous session ended without stopping tells the reader the workspace was recovered.
TEST_F(ProductTest, TellsTheReaderTheWorkspaceWasRecovered) {
    TemporaryDirectory data;
    std::ofstream(data.path() / "workpane.lock") << "running";

    // The notice waits for the workspace and stays for its whole lifetime from there, however long the plugins took to start.
    {
        HeadlessProduct product(data.path());
        ASSERT_TRUE(product.boot().hasValue());
        // clang-format off
        const auto shown = [&product]() { return product.product().shell().toasts().showing("Workspace recovered", "The previous session did not close cleanly and its saved state was restored"); };
        // clang-format on
        const int lifetime = static_cast<int>(ui::ToastOverlay::lifetimeSeconds / HeadlessProduct::frameSeconds);
        EXPECT_TRUE(product.frameUntil(shown));

        for (int frame = 0; frame < lifetime - 30; ++frame) {
            product.frame();
        }

        EXPECT_TRUE(shown());

        for (int frame = 0; frame < 60; ++frame) {
            product.frame();
        }

        EXPECT_FALSE(shown());
        product.stop();
    }

    // The session that stopped cleanly leaves nothing to recover.
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    product.settle(std::chrono::milliseconds(100));
    EXPECT_FALSE(product.product().shell().toasts().showing("Workspace recovered", "The previous session did not close cleanly and its saved state was restored"));
    product.stop();
}

// A text is measured in points in the face and the size a canvas draws it with, in the language of the reader, and a size a canvas refuses is refused.
TEST_F(ProductTest, MeasuresTextTheWayACanvasDrawsIt) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    const auto loaded = product.product().runtime().loadString(R"(
        local bridge = require("workpane.bridge")
        local workpane = require("workpane.api").create("logs", "", { version = "", debug = false, platform = "", architecture = "", paths = { data = "" }, languages = {}, themes = {}, icons = {}, colors = {} })
        local short = workpane.ui.textWidth("Level 3", { size = 11, face = "semibold" })
        local long = workpane.ui.textWidth("Level 3 of many", { size = 11, face = "semibold" })
        local larger = workpane.ui.textWidth("Level 3", { size = 22, face = "semibold" })
        local translated = workpane.ui.textWidth(workpane.i18n.text("task-hero.band.level", 3), { size = 11, face = "semibold" })
        local _, refused = pcall(workpane.ui.textWidth, "Level 3", { size = 2 })
        bridge.call("workpane_log", { plugin = "logs", level = "info", category = "suite", message = "measured", details = { short = short, long = long, larger = larger, translated = translated, refused = refused.code } })
    )",
                                                               "measure");
    ASSERT_TRUE(loaded.hasValue()) << loaded.error().message;
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.query("SELECT details_json FROM logs__entries WHERE message = 'measured'").empty(); }));
    // clang-format on
    const json measured = json::parse(product.query("SELECT details_json FROM logs__entries WHERE message = 'measured'")[0]["details_json"].get<std::string>());
    EXPECT_GT(measured["short"].get<double>(), 20.0);
    EXPECT_GT(measured["long"].get<double>(), measured["short"].get<double>());
    EXPECT_NEAR(measured["larger"].get<double>(), 2.0 * measured["short"].get<double>(), 0.1 * measured["short"].get<double>());
    EXPECT_DOUBLE_EQ(measured["translated"].get<double>(), measured["short"].get<double>());
    EXPECT_EQ(measured["refused"], "json_field_range");
    product.stop();
}

TEST_F(ProductTest, ShowsTheMachineInCardsAndKeepsItWhenARefreshFails) {
    TemporaryDirectory data;
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    ASSERT_TRUE(product.navigate("system-information:overview").hasValue());
    const std::string surface = "view:system-information:overview";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(surface)).showsKey("system-information.section.network"); }));
    // clang-format on

    for (const std::string_view section : {"overview", "operating-system", "processor", "memory", "graphics", "mainboard", "storage", "batteries"}) {
        EXPECT_TRUE(SurfaceReader(product.declared(surface)).showsKey("system-information.section." + std::string(section))) << section;
    }

    EXPECT_TRUE(SurfaceReader(product.declared(surface)).showsText("Test Processor"));
    EXPECT_TRUE(SurfaceReader(product.declared(surface)).showsText("Test SSD"));
    EXPECT_TRUE(SurfaceReader(product.declared(surface)).showsText("Test Display"));
    EXPECT_TRUE(SurfaceReader(product.declared(surface)).showsKey("system-information.view.updated"));
    EXPECT_TRUE(SurfaceReader(product.declared(surface)).showsKey("system-information.field.threads"));
    EXPECT_EQ(product.system().inspections, 1);

    product.system().inspectionFails = true;
    const auto refresh = product.firstNode(surface, "button");
    ASSERT_TRUE(refresh.has_value());
    product.emit(surface, *refresh, "click", json::object());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.system().inspections == 2 && stored(data.path(), "The hardware could not be read (code \"system_inspection_failed\", detail \"test\")"); }));
    // clang-format on

    EXPECT_TRUE(SurfaceReader(product.declared(surface)).showsText("Test Processor"));
    EXPECT_TRUE(SurfaceReader(product.declared(surface)).showsKey("system-information.common.little-endian"));

    // A byte order the system does not name is shown as unavailable rather than guessed.
    product.system().inspectionFails = false;
    product.system().snapshot = FakeSystemInspector::sample();
    product.system().snapshot["os"]["byteOrder"] = "";
    product.emit(surface, *refresh, "click", json::object());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.system().inspections == 3 && !SurfaceReader(product.declared(surface)).showsKey("system-information.common.little-endian"); }));
    // clang-format on
    EXPECT_FALSE(SurfaceReader(product.declared(surface)).showsKey("system-information.common.big-endian"));
    EXPECT_TRUE(SurfaceReader(product.declared(surface)).showsKey("system-information.field.used"));

    // Memory whose available amount is unknown shows no share in use, and a display without a scale shows it as unavailable instead of zero.
    product.system().snapshot["memory"]["available"] = 0;
    product.system().displays[0]["scale"] = 0.0;
    product.emit(surface, *refresh, "click", json::object());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.system().inspections == 4 && !SurfaceReader(product.declared(surface)).showsKey("system-information.field.used"); }));
    // clang-format on
    const SurfaceReader reader(product.declared(surface));
    const auto scale = reader.nodeShowing("system-information.field.scale");
    ASSERT_TRUE(scale.has_value());
    EXPECT_EQ(reader.properties(*scale + 1).value("text", json::object()).value("key", ""), "system-information.common.unavailable");
    product.stop();
    EXPECT_EQ(storedErrors(data.path()).size(), 1U);
}

} // namespace workpane::tests
