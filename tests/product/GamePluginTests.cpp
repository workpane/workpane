#include "Error.h"
#include "app/Product.h"
#include "persistence/PreferenceStore.h"
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
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::tests {

using nlohmann::json;

class GamePluginTest : public ::testing::Test {
  protected:
    // Answers the names of the sound files the product played, in order.
    static std::vector<std::string> played(HeadlessProduct& product) {
        std::vector<std::string> names;

        for (const auto& sound : product.audio().played) {
            names.push_back(sound.file.stem().string());
        }

        return names;
    }

    // Turns a game on in the core preferences, since the games wait for the reader to turn them on, and only one runs so the canvas of the other never draws beside it.
    static void play(const TemporaryDirectory& data, std::string_view game) {
        auto database = tests::ProductDatabase::open(data);
        ASSERT_TRUE(persistence::PreferenceStore::store(database, "workpane", {{"pluginSwitches", {{game, true}}}}).hasValue());
    }

    // Opens the settings and searches them, which is how a test reaches the section of one plugin among all of them.
    static void search(HeadlessProduct& product, std::string_view text) {
        ASSERT_TRUE(product.navigate(ui::Shell::settingsDestination).hasValue());
        product.frame();
        product.frame();
        product.press(HeadlessProduct::command() | ImGuiKey_F);
        product.frame();
        product.type(text);
    }

    // Emits an event on the first node of a kind in a surface, which is how a test acts on a control it has not measured.
    static void act(HeadlessProduct& product, const std::string& surface, std::string_view kind, std::string_view name, json value) {
        product.emit(surface, SurfaceReader(product.declared(surface)).nodes(kind).front(), name, std::move(value));
    }

    [[nodiscard]] static std::vector<json> errors(const HeadlessProduct& product) {
        return product.query("SELECT source, category, message, details_json FROM logs__entries WHERE level = 'error'");
    }
};

// A flap starts a round with its sounds, Escape pauses it without ticking, a round that falls to the ground is kept among the best rounds even on a machine that cannot play its sounds, and a round with the sound turned off plays nothing.
TEST_F(GamePluginTest, FliesARoundOfFlappyBirdAndKeepsItAmongTheBest) {
    TemporaryDirectory data;
    play(data, "flappy-bird");
    HeadlessProduct product(data.path());
    product.audio().failures.emplace("hit.wav", Error{"audio_unavailable", "The machine has no audio device the product could open", "hit.wav"});
    ASSERT_TRUE(product.boot().hasValue());
    ASSERT_TRUE(product.navigate("flappy-bird:game").hasValue());
    const std::string view = "view:flappy-bird:game";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(view) != nullptr && !SurfaceReader(product.declared(view)).nodes("canvas").empty(); }));
    // clang-format on

    act(product, view, "canvas", "key-down", {{"key", "space"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return played(product).size() >= 2; }));
    // clang-format on

    // Escape pauses the round, which stops ticking until Escape resumes it.
    const ui::NodeId canvas = SurfaceReader(product.declared(view)).nodes("canvas").front();
    act(product, view, "canvas", "key-down", {{"key", "escape"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(view)).properties(canvas)["frameRate"] == 0; }));
    // clang-format on
    act(product, view, "canvas", "key-down", {{"key", "escape"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(view)).properties(canvas)["frameRate"] == 60; }));
    // clang-format on
    const auto started = played(product);
    EXPECT_EQ(std::vector<std::string>(started.begin(), started.begin() + 2), (std::vector<std::string>{"swoosh", "wing"}));
    EXPECT_FLOAT_EQ(product.audio().played.front().volume, 0.6F);

    const std::string rounds = "SELECT score FROM flappy_bird__rounds";
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.query(rounds).empty(); }));
    // clang-format on
    EXPECT_EQ(product.query(rounds)[0]["score"], 0);
    const auto ended = played(product);
    EXPECT_NE(std::ranges::find(ended, "hit"), ended.end());

    // The board stops ticking once it can start the next round, and draws nothing more while it waits.
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(view)).properties(canvas)["frameRate"] == 0; }));
    // clang-format on
    product.recordCalls("workpane_ui_command");
    const std::size_t drawn = product.calls("workpane_ui_command");
    product.settle(std::chrono::milliseconds(200));
    EXPECT_EQ(product.calls("workpane_ui_command"), drawn);

    // Forgetting the best rounds while the best score is already zero clears them from the board too.
    // clang-format off
    const auto boardLines = [&]() { const json list = product.lastCall("workpane_ui_command"); std::size_t lines = 0; for (const auto& command : list["arguments"]["commands"]) { lines += command["op"] == "text" ? 1U : 0U; } return lines; };
    // clang-format on
    const std::string settingsSection = "settings:flappy-bird:flappy-bird:general";
    search(product, "Bird");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(settingsSection) != nullptr; }));
    // clang-format on
    const auto reset = SurfaceReader(product.declared(settingsSection)).nodeShowing("flappy-bird.settings.reset");
    ASSERT_TRUE(reset.has_value());
    product.emit(settingsSection, *reset, "click", json::object());
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.query(rounds).empty() && product.calls("workpane_ui_command") > drawn; }));
    // clang-format on
    EXPECT_EQ(boardLines(), 4U);
    ASSERT_TRUE(product.navigate("flappy-bird:game").hasValue());
    product.frame();

    // A combination of the product leaves the game while its canvas has the keyboard, even pressed in the same frame as its modifier.
    product.press(HeadlessProduct::command() | ImGuiKey_Comma);
    EXPECT_EQ(product.product().shell().destination(), ui::Shell::settingsDestination);

    const std::string section = "settings:flappy-bird:flappy-bird:general";
    search(product, "Bird");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(section) != nullptr; }));
    // clang-format on
    act(product, section, "toggle", "change", {{"checked", false}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().preferences().document("flappy-bird").value("sound", true) == false; }));
    // clang-format on

    const std::size_t before = product.audio().played.size();
    ASSERT_TRUE(product.navigate("flappy-bird:game").hasValue());
    product.settle(std::chrono::milliseconds(50));
    act(product, view, "canvas", "key-down", {{"key", "space"}});
    act(product, view, "canvas", "key-down", {{"key", "space"}});
    product.settle(std::chrono::milliseconds(200));
    EXPECT_EQ(product.audio().played.size(), before);
    product.stop();
}

// The adventure runs by itself in a band at the bottom of the window, keeps the progress of its knight with the kind of warrior he is, stops ticking while paused even when the reader starts over, and its band follows the height the reader chooses.
TEST_F(GamePluginTest, RunsTheAdventureOfTaskHeroInABandAtTheBottom) {
    TemporaryDirectory data;
    play(data, "task-hero");
    HeadlessProduct product(data.path());
    ASSERT_TRUE(product.boot().hasValue());
    const std::string band = "band:task-hero:adventure";
    // clang-format off
    const auto height = [&product]() {
        for (const auto& item : product.product().shell().bands()) {
            if (item.plugin == "task-hero") {
                return item.height;
            }
        }

        return std::int64_t{-1};
    };

    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(band) != nullptr && height() == 88; }));
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().preferences().document("task-hero").value("progress", json::object()).value("victories", 0) >= 1; }, std::chrono::milliseconds(60000)));
    // clang-format on
    EXPECT_EQ(product.product().preferences().document("task-hero")["progress"]["class"], "warrior");

    // A click pauses the adventure, which stops ticking until the next click resumes it.
    const ui::NodeId canvas = SurfaceReader(product.declared(band)).nodes("canvas").front();
    product.emit(band, canvas, "pointer-down", {{"x", 10}, {"y", 10}, {"button", "left"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(band)).properties(canvas)["frameRate"] == 0; }));
    // clang-format on

    // Starting over while paused keeps the adventure paused, so the next click alone resumes it.
    const std::string section = "settings:task-hero:task-hero:general";
    search(product, "Band height");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.surface(section) != nullptr; }));
    // clang-format on
    act(product, section, "button", "click", json::object());
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().preferences().document("task-hero").value("progress", json::object()).value("victories", 0) == 0; }));
    // clang-format on
    product.settle(std::chrono::milliseconds(200));
    EXPECT_EQ(SurfaceReader(product.declared(band)).properties(canvas)["frameRate"], 0);
    product.emit(band, canvas, "pointer-down", {{"x", 10}, {"y", 10}, {"button", "left"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(band)).properties(canvas)["frameRate"] == 30; }));
    // clang-format on
    act(product, section, "combo", "change", {{"value", "large"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return height() == 120; }));
    // clang-format on

    // Day and night starts off and, once turned on, lights the band by the hour of the clock.
    const SurfaceReader settings(product.declared(section));
    const ui::NodeId dayNight = settings.nodes("toggle")[1];
    EXPECT_FALSE(product.product().preferences().document("task-hero").value("dayNight", true));
    EXPECT_EQ(settings.properties(dayNight)["checked"], false);
    product.emit(section, dayNight, "change", {{"checked", true}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().preferences().document("task-hero").value("dayNight", false); }));
    // clang-format on
    product.settle(std::chrono::milliseconds(300));
    EXPECT_TRUE(std::isfinite(product.demand().deadline));
    act(product, section, "combo", "change", {{"value", "hidden"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return height() == 0; }));
    // clang-format on
    EXPECT_NE(product.surface(band), nullptr);

    // A hidden adventure ticks no more, so a window with nothing else animating asks for no frame at all.
    ASSERT_TRUE(product.navigate("donate:support").hasValue());
    product.settle(std::chrono::milliseconds(500));
    EXPECT_FALSE(product.demand().animating);
    EXPECT_FALSE(std::isfinite(product.demand().deadline));
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

} // namespace workpane::tests
