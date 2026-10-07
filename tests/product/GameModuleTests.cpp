#include "support/Resources.h"
#include "support/ScriptHarness.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <numbers>
#include <string>
#include <string_view>

namespace workpane::tests {

using nlohmann::json;

// Loads the rules of the Flappy Bird and Task Hero plugins by themselves and drives them tick by tick, with a sequence of numbers standing in for chance.
class GameModuleTest : public ::testing::Test {
  protected:
    [[nodiscard]] static json run(std::string_view plugin, std::string_view body) {
        const std::string directory = (Resources::staged() / "plugins" / std::string(plugin)).generic_string();
        const std::string prelude = R"(
            local directory = [==[)" +
                                    directory + R"(]==]
            local loaded = {}
            local environment = setmetatable({}, { __index = _G })
            local draws = 0

            local function translate(key, ...)
                return { key = key, args = { ... } }
            end

            local function random()
                draws = draws + 1
                return (draws * 0.37) % 1
            end

            -- A text is measured as six tenths of its size for every character of the last part of its key and of its arguments, which is enough to lay out a drawing.
            local function textWidth(value, options)
                local written = value.key:match("[^.]+$")

                for _, argument in ipairs(value.args) do
                    written = written .. tostring(argument)
                end

                return #written * options.size * 0.6
            end

            environment.workpane = { i18n = { text = translate, number = function(value) return value end }, ui = { textWidth = textWidth } }

            function environment.include(name)
                if loaded[name] == nil then
                    loaded[name] = assert(loadfile(directory .. "/" .. name .. ".lua", "t", environment))()
                end

                return loaded[name]
            end

            local include = environment.include
        )";
        ScriptHarness harness;
        const auto ran = harness.run(prelude + std::string(body));
        EXPECT_TRUE(ran.hasValue()) << (ran.hasValue() ? "" : ran.error().message);

        return harness.results.size() == 1 ? harness.results.front() : json();
    }

    struct Shadow final {
        double centre;
        int bottom;
    };

    // Finds the shadow the pack draws under the feet of a figure in every frame of a sheet and answers the mean of its middles and its lowest bottom, which is where the figure stands.
    [[nodiscard]] static Shadow shadow(const std::filesystem::path& file, int frameWidth, int frames) {
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load(file.string().c_str(), &width, &height, &channels, 4);
        Shadow found{0.0, 0};

        if (pixels == nullptr) {
            return found;
        }

        for (int frame = 0; frame < frames; ++frame) {
            int least = frameWidth;
            int most = -1;

            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < frameWidth; ++x) {
                    const stbi_uc* pixel = pixels + (static_cast<std::ptrdiff_t>(y) * width + frame * frameWidth + x) * 4;

                    if (pixel[0] == 15 && pixel[1] == 18 && pixel[2] == 26 && pixel[3] == 79) {
                        least = std::min(least, x);
                        most = std::max(most, x);
                        found.bottom = std::max(found.bottom, y + 1);
                    }
                }
            }

            found.centre += static_cast<double>(least + most + 1) / 2.0 / static_cast<double>(frames);
        }

        stbi_image_free(pixels);

        return found;
    }
};

// A round waits for the first flap, falls under gravity to the ground without flaps, scores a point for a pipe passed and ends on a pipe it strikes, and a new round starts only a moment after the last one ended.
TEST_F(GameModuleTest, PlaysFlappyBirdByItsOriginalRules) {
    const json results = run("flappy-bird", R"(
        local game = include("game")
        local function steps(state, seconds)
            local sounds = {}
            for _ = 1, math.floor(seconds / 0.02) do
                for _, sound in ipairs(game.step(state, 0.02)) do
                    sounds[#sounds + 1] = sound
                end
            end
            return table.concat(sounds, ",")
        end

        local state = game.new(1000, random)
        steps(state, 1)
        local waiting = { mode = state.mode, pipes = #state.pipes, width = state.width, bobbing = state.birdY > 239 and state.birdY < 249 }
        local started = table.concat(game.flap(state), ",")
        local firstPipe = state.pipes[1].x
        local fell = ""
        while state.mode ~= "over" do
            fell = fell .. steps(state, 0.02)
        end
        local ended = state.mode
        local early = #game.flap(state)
        steps(state, 0.7)
        local again = table.concat(game.flap(state), ",")

        game.flap(state)
        state.pipes = { { x = 40, gapY = 200, scored = false } }
        local scored = steps(state, 0.1)

        local struck = game.new(288, random)
        game.flap(struck)
        struck.pipes = { { x = 70, gapY = 60, scored = false } }
        local strike = steps(struck, 0.04)

        local paused = game.new(288, random)
        game.flap(paused)
        game.pause(paused)
        local height = paused.birdY
        local silent = steps(paused, 0.5)

        host.test_result({ waiting = waiting, started = started, firstPipe = firstPipe, fell = fell, ended = ended, early = early, again = again, restarted = state.score, scored = scored, score = state.score, strike = strike, struckMode = struck.mode, silent = silent, still = paused.birdY == height })
    )");

    EXPECT_EQ(results["waiting"]["mode"], "ready");
    EXPECT_EQ(results["waiting"]["pipes"], 0);
    EXPECT_EQ(results["waiting"]["width"], 432);
    EXPECT_TRUE(results["waiting"]["bobbing"]);
    EXPECT_EQ(results["started"], "swoosh,wing");
    EXPECT_EQ(results["firstPipe"], 532);
    EXPECT_EQ(results["fell"], "hit");
    EXPECT_EQ(results["ended"], "over");
    EXPECT_EQ(results["early"], 0);
    EXPECT_EQ(results["again"], "swoosh");
    EXPECT_EQ(results["scored"], "point");
    EXPECT_EQ(results["score"], 1);
    EXPECT_EQ(results["strike"], "hit,die");
    EXPECT_EQ(results["struckMode"], "falling");
    EXPECT_EQ(results["silent"], "");
    EXPECT_TRUE(results["still"]);
}

// A round is drawn over the backgrounds of its time of day, with the pipes clipped to the field, the sides beyond a narrower field shaded, the bird turned with its fall and the board with the best rounds once it ends.
TEST_F(GameModuleTest, DrawsFlappyBirdForTheTimeOfDayAndTheEndOfARound) {
    const json results = run("flappy-bird", R"(
        local drawing = include("drawing")
        local game = include("game")
        local state = game.new(1000, random)
        game.flap(state)
        state.pipes = { { x = 200, gapY = 150, scored = false } }
        state.turn = -60
        local images = {}
        local shades = 0
        local bird
        for _, command in ipairs(drawing.commands(state, { width = 1000, height = 512 }, { bird = "blue", night = true, best = 3, rounds = {} })) do
            shades = shades + ((command.op == "rect" and command.y == 0 and command.height == 512) and 1 or 0)
            if command.op == "image" then
                images[command.image] = (images[command.image] or 0) + 1
                if command.image:find("bluebird") then
                    bird = command
                end
            end
        end
        state.mode = "over"
        local board = 0
        for _, command in ipairs(drawing.commands(state, { width = 1000, height = 512 }, { bird = "blue", night = false, best = 3, rounds = { { score = 3, date = "today" } } })) do
            if command.op == "text" then
                board = board + 1
            end
        end
        local narrow = 0
        for _, command in ipairs(drawing.commands(state, { width = 432, height = 512 }, { bird = "blue", night = false, best = 3, rounds = {} })) do
            narrow = narrow + (command.op == "rect" and 1 or 0)
        end
        local tiled = {}
        for _, command in ipairs(drawing.commands(state, { width = 1000, height = 512 }, { bird = "blue", night = true, best = 3, rounds = {} })) do
            if command.tile ~= nil then
                tiled[#tiled + 1] = { image = command.image, x = command.x, width = command.width, tile = command.tile.width }
            end
        end
        local short = #drawing.commands(state, { width = 1000, height = 4 }, { bird = "blue", night = true, best = 3, rounds = {} })
        host.test_result({ night = images["sprites/background-night.png"] or 0, pipes = images["sprites/pipe-red.png"] or 0, rotation = bird.rotation, board = board, shades = shades, narrow = narrow, tiled = tiled, short = short })
    )");

    // The background and the ground each repeat in one command from a tile left of the canvas, and a canvas too short to show the game draws nothing.
    EXPECT_EQ(results["night"], 1);
    ASSERT_EQ(results["tiled"].size(), 2U);
    EXPECT_EQ(results["tiled"][0]["image"], "sprites/background-night.png");
    EXPECT_EQ(results["tiled"][0]["tile"], 288);
    EXPECT_LE(results["tiled"][0]["x"].get<double>(), 0.0);
    EXPECT_GE(results["tiled"][0]["x"].get<double>() + results["tiled"][0]["width"].get<double>(), 1000.0);
    EXPECT_EQ(results["tiled"][1]["image"], "sprites/base.png");
    EXPECT_EQ(results["tiled"][1]["tile"], 336);
    EXPECT_EQ(results["short"], 0);
    EXPECT_EQ(results["pipes"], 2);
    EXPECT_NEAR(results["rotation"].get<double>(), 60.0 * 3.14159265358979 / 180.0, 1e-6);
    EXPECT_EQ(results["board"], 5);
    EXPECT_EQ(results["shades"], 2);
    EXPECT_EQ(results["narrow"], 1);
}

// Every picture a game draws is one its canvas keeps ready from the start, so no figure goes missing while its sheet is decoded the first time it shows.
TEST_F(GameModuleTest, NamesEveryPictureTheGamesDraw) {
    const json hero = run("task-hero", R"(
        local adventure = include("adventure")
        local daylight = include("daylight")
        local drawing = include("drawing")
        local sheets = include("sheets")
        local night = daylight.night(1)
        local named = {}
        for _, picture in ipairs(sheets.pictures()) do
            named[picture] = true
        end
        local missing = {}
        local drawn = {}
        for _, class in ipairs(sheets.classNames) do
            local state = adventure.new({ gold = 0, level = 1, experience = 0, victories = 0, class = class }, random)
            adventure.resize(state, 900)
            for step = 1, 1500 do
                adventure.step(state, 0.05, 1)
                for _, command in ipairs(drawing.commands(state, { width = 900, height = 96 }, true, step % 2 == 0 and night or nil)) do
                    if command.op == "image" then
                        drawn[command.image] = true
                        if not named[command.image] then
                            missing[#missing + 1] = command.image
                        end
                    end
                end
            end
        end
        local count = 0
        for _ in pairs(drawn) do
            count = count + 1
        end
        host.test_result({ missing = missing, drawn = count, named = #sheets.pictures() })
    )");
    const json flappy = run("flappy-bird", R"(
        local drawing = include("drawing")
        local game = include("game")
        local colors = { "yellow", "blue", "red" }
        local named = {}
        for _, picture in ipairs(drawing.pictures(colors)) do
            named[picture] = true
        end
        local missing = {}
        for _, color in ipairs(colors) do
            for _, night in ipairs({ false, true }) do
                local state = game.new(1000, random)
                for step = 1, 200 do
                    if step == 2 or step % 8 == 0 then
                        game.flap(state)
                    end
                    game.step(state, 0.05)
                    state.score = step
                    for _, command in ipairs(drawing.commands(state, { width = 1000, height = 512 }, { bird = color, night = night, best = 3, rounds = {} })) do
                        if command.op == "image" and not named[command.image] then
                            missing[#missing + 1] = command.image
                        end
                    end
                end
            end
        end
        host.test_result({ missing = missing })
    )");

    EXPECT_TRUE(hero["missing"].empty()) << hero["missing"].dump();
    EXPECT_GT(hero["drawn"].get<int>(), 20);
    EXPECT_TRUE(flappy["missing"].empty()) << flappy["missing"].dump();
}

// The knight walks through a landscape that fills itself, beats a foe within his reach for its gold and experience, levels up, falls to a foe too strong and rises again, and a pause stops everything.
TEST_F(GameModuleTest, RunsTheAdventureOfTheKnightByItself) {
    const json results = run("task-hero", R"(
        local adventure = include("adventure")
        local golds = 0
        local function steps(state, seconds)
            local events = {}
            for _ = 1, math.floor(seconds / 0.05 + 0.5) do
                for _, event in ipairs(adventure.step(state, 0.05, 1)) do
                    golds = golds + (event == "gold" and 1 or 0)
                    events[#events + 1] = event ~= "gold" and event or nil
                end
            end
            return table.concat(events, ",")
        end

        local state = adventure.new({ gold = 0, level = 1, experience = 0, victories = 0, class = "warrior" }, random)
        adventure.resize(state, 400)
        steps(state, 0.5)
        local walked = { shift = state.groundShift > 0, scenery = #state.scenery > 0, foes = #state.foes, action = state.hero.action }

        local hero = adventure.heroX()
        local blocker = { kind = "sheep", x = 5000, health = 12, most = 12, damage = 0, action = "idle", swing = 0, cooldown = 0 }
        state.random = function() return 0.99 end
        state.foes = { { kind = "red-warrior", x = hero + 20, health = 20, most = 20, damage = 1, action = "idle", swing = 0, cooldown = 0 }, blocker }
        state.progress.experience = adventure.needed(1) - 1
        local won = steps(state, 4)
        local after = { gold = state.progress.gold, level = state.progress.level, victories = state.progress.victories, health = state.hero.health, items = #state.items }

        state.foes = { { kind = "black-warrior", x = hero + 20, health = 1000, most = 1000, damage = 500, action = "idle", swing = 0, cooldown = 0 }, blocker }
        local fell = steps(state, 1.2)
        local resting = state.hero.resting > 0
        steps(state, 2.6)
        local risen = state.hero.health == adventure.health(state.progress.level)

        state.foes = { { kind = "purple-archer", x = hero + 90, health = 1000, most = 1000, damage = 5, action = "idle", swing = 0, cooldown = 0 }, blocker }
        local before = state.hero.health
        steps(state, 0.2)
        state.foes[1].x = hero + 90
        steps(state, 1.6)
        local shot = state.hero.health < before

        adventure.pause(state)
        local shift = state.groundShift
        local paused = steps(state, 1)

        host.test_result({ walked = walked, won = won, after = after, fell = fell, resting = resting, risen = risen, shot = shot, paused = paused, still = state.groundShift == shift })
    )");

    EXPECT_TRUE(results["walked"]["shift"]);
    EXPECT_TRUE(results["walked"]["scenery"]);
    EXPECT_GE(results["walked"]["foes"], 1);
    EXPECT_EQ(results["walked"]["action"], "run");
    EXPECT_EQ(results["won"], "victory,level");
    EXPECT_EQ(results["after"]["gold"], 3);
    EXPECT_EQ(results["after"]["level"], 2);
    EXPECT_EQ(results["after"]["victories"], 1);
    EXPECT_EQ(results["after"]["health"], 72);
    EXPECT_EQ(results["after"]["items"], 0);
    EXPECT_EQ(results["fell"], "fall");
    EXPECT_TRUE(results["resting"]);
    EXPECT_TRUE(results["risen"]);
    EXPECT_TRUE(results["shot"]);
    EXPECT_EQ(results["paused"], "");
    EXPECT_TRUE(results["still"]);
}

// Foes enter from beyond the right edge and wait in a line, never more than three, the knight keeps his place when the band is resized, items land a little past the middle, and a foe behind the knight leaves at the left edge without striking or rewarding him.
TEST_F(GameModuleTest, BringsFoesFromTheRightAndKeepsThePlacesAcrossAResize) {
    const json results = run("task-hero", R"(
        local adventure = include("adventure")
        local golds = 0
        local function steps(state, seconds)
            local events = {}
            for _ = 1, math.floor(seconds / 0.05 + 0.5) do
                for _, event in ipairs(adventure.step(state, 0.05, 1)) do
                    golds = golds + (event == "gold" and 1 or 0)
                    events[#events + 1] = event ~= "gold" and event or nil
                end
            end
            return table.concat(events, ",")
        end

        local state = adventure.new({ gold = 0, level = 1, experience = 0, victories = 0, class = "warrior" }, random)
        adventure.resize(state, 1400)
        adventure.step(state, 0.05, 1)
        local entered = state.foes[1].x > 1400
        local most = 0
        for _ = 1, 400 do
            adventure.step(state, 0.05, 1)
            local count = 0
            for _, foe in ipairs(state.foes) do
                count = count + (foe.x > adventure.heroX() - 4 and 1 or 0)
            end
            most = math.max(most, count)
        end

        local hero = adventure.heroX()
        adventure.resize(state, 500)
        local kept = adventure.heroX() == hero

        local spots = {}
        state.items = {}
        for index, width in ipairs({ 400, 900, 1400 }) do
            adventure.resize(state, width)
            local spot = adventure.landing(state)
            spots[index] = { width = width, spot = spot }
        end

        local line = {}
        for index = 1, 3 do
            line[index] = { kind = "red-warrior", x = hero + 60 * index, health = 1000, most = 1000, damage = 1, action = "idle", swing = 0, cooldown = 0 }
        end
        state.foes = line
        state.hero.resting = 5
        steps(state, 3)
        local xs = {}
        for index, foe in ipairs(state.foes) do
            xs[index] = foe.x
        end
        table.sort(xs)

        local fresh = adventure.new({ gold = 0, level = 1, experience = 0, victories = 0, class = "warrior" }, random)
        adventure.resize(fresh, 600)
        fresh.foes = { { kind = "black-warrior", x = hero - 40, health = 10, most = 10, damage = 50, action = "idle", swing = 0, cooldown = 0 }, { kind = "sheep", x = 5000, health = 12, most = 12, damage = 0, action = "idle", swing = 0, cooldown = 0 } }
        local behind = fresh.foes[1]
        local passed = steps(fresh, 6)
        local gone = true
        for _, foe in ipairs(fresh.foes) do
            gone = gone and foe ~= behind
        end

        host.test_result({ entered = entered, most = most, kept = kept, spots = spots, xs = xs, count = #state.foes, passed = passed, gone = gone, health = fresh.hero.health, victories = fresh.progress.victories, hero = hero })
    )");

    const double hero = results["hero"].get<double>();
    EXPECT_TRUE(results["entered"]);
    EXPECT_GE(results["most"], 2);
    EXPECT_LE(results["most"], 3);
    EXPECT_TRUE(results["kept"]);

    for (const auto& spot : results["spots"]) {
        const double width = spot["width"].get<double>();
        const double where = spot["spot"].get<double>();
        EXPECT_GE(where, std::min(hero + 160.0, width - 40.0)) << width;
        EXPECT_LE(where, width - 40.0) << width;
        EXPECT_GE(where, std::min(width * 0.5, width - 40.0)) << width;
    }

    EXPECT_EQ(results["count"], 3);
    // A red warrior waits 37 pixels right and 47 pixels left of its middle, drawn at half size and two units apart.
    EXPECT_GE(results["xs"][1].get<double>() - results["xs"][0].get<double>(), 44.0 - 1e-9);
    EXPECT_GE(results["xs"][2].get<double>() - results["xs"][1].get<double>(), 44.0 - 1e-9);
    EXPECT_EQ(results["passed"], "");
    EXPECT_TRUE(results["gone"]);
    EXPECT_EQ(results["health"], 60);
    EXPECT_EQ(results["victories"], 0);
}

// A strike plays the attack of the knight from its first frame, the blow lands on its impact frame, a strike that kills keeps its attack to the last frame before he walks on, the beaten foe leaves smoke, a sheep fills his health in a glow of healing, and the next strike takes the other attack once the interval ends.
TEST_F(GameModuleTest, PlaysEachAttackToItsEndAndLandsItsBlowOnTheImpactFrame) {
    const json results = run("task-hero", R"(
        local adventure = include("adventure")
        local drawing = include("drawing")
        local sheets = include("sheets")
        local attack = sheets.classes.warrior.attack
        local idle = sheets.classes.warrior.idle
        local function pose(seconds)
            local sheet, frame = sheets.strike(attack, idle, seconds)
            return (sheet == attack and "attack" or "idle") .. frame
        end

        local function fresh()
            local state = adventure.new({ gold = 0, level = 1, experience = 0, victories = 0, class = "warrior" }, random)
            adventure.resize(state, 400)
            state.random = function() return 0.99 end
            return state
        end

        local function heroImage(state)
            local found
            for _, command in ipairs(drawing.commands(state, { width = 400, height = 96 }, false)) do
                if command.op == "image" and command.image:find("blue%-warrior") then
                    found = command.image
                end
            end
            return found
        end

        local blocker = { kind = "sheep", x = 5000, health = 12, most = 12, damage = 0, action = "idle", swing = 0, cooldown = 0 }
        local hero = adventure.heroX()
        local state = fresh()
        state.foes = { { kind = "red-warrior", x = hero + 20, health = 1000, most = 1000, damage = 1, action = "idle", swing = 5, cooldown = 1 }, blocker }
        local seen = {}
        adventure.step(state, 0.05, 1)
        seen[#seen + 1] = heroImage(state)
        local untouched = state.foes[1].health
        adventure.step(state, 0.5, 1)
        seen[#seen + 1] = heroImage(state)
        local struck = state.foes[1].health
        adventure.step(state, 0.35, 1)
        seen[#seen + 1] = heroImage(state)

        local killing = fresh()
        killing.foes = { { kind = "sheep", x = hero + 20, health = 1, most = 1, damage = 0, action = "idle", swing = 0, cooldown = 0 }, blocker }
        killing.hero.health = 10
        adventure.step(killing, 0.1, 1)
        local before = table.concat(adventure.step(killing, 0.1, 1), ",")
        local alive = #killing.foes == 2
        local killed = table.concat(adventure.step(killing, 0.1, 1), ",")
        local smoke = false
        local glow = false
        for _, shown in ipairs(killing.effects) do
            smoke = smoke or shown.kind == "smoke"
            glow = glow or shown.kind == "heal"
        end
        local holding = { action = killing.hero.action, image = heroImage(killing) }
        local fed = killing.hero.health

        local avenged = fresh()
        local doomed = { kind = "red-warrior", x = hero + 20, health = 1, most = 1, damage = 50, action = "attack", swing = 0, cooldown = 1, blow = { at = 0, damage = 50, shoots = false, from = hero + 12 } }
        avenged.foes = { doomed, blocker }
        avenged.hero.action = "attack"
        avenged.hero.blow = { at = 0, target = doomed, damage = 10, shoots = false, from = hero + 8 }
        adventure.step(avenged, 0.05, 1)
        local spared = avenged.hero.health == adventure.health(1) and avenged.progress.victories == 1
        adventure.step(killing, 0.1, 1)
        local still = killing.hero.action
        adventure.step(killing, 0.1, 1)
        local walking = killing.hero.action

        host.test_result({ start = pose(0), last = pose(0.3), after = pose(0.34), later = pose(0.75), impact = sheets.impact(attack), seen = table.concat(seen, ","), untouched = untouched, struck = struck, before = before, alive = alive, killed = killed, smoke = smoke, glow = glow, holding = holding, fed = fed, spared = spared, still = still, walking = walking })
    )");

    EXPECT_EQ(results["start"], "attack0");
    EXPECT_EQ(results["last"], "attack3");
    EXPECT_EQ(results["after"], "idle0");
    EXPECT_EQ(results["later"], "idle4");
    EXPECT_NEAR(results["impact"].get<double>(), 2.0 / 12.0, 1e-9);
    EXPECT_EQ(results["seen"], "units/blue-warrior-attack.png,units/blue-warrior-idle.png,units/blue-warrior-attack-2.png");
    EXPECT_EQ(results["untouched"], 1000);
    EXPECT_EQ(results["struck"], 1000 - 8);
    EXPECT_EQ(results["before"], "");
    EXPECT_TRUE(results["alive"]);
    EXPECT_EQ(results["killed"], "victory");
    EXPECT_TRUE(results["smoke"]);
    EXPECT_EQ(results["fed"], 60);
    EXPECT_TRUE(results["glow"]);
    EXPECT_EQ(results["holding"]["action"], "attack");
    EXPECT_EQ(results["holding"]["image"], "units/blue-warrior-attack.png");
    EXPECT_TRUE(results["spared"]);
    EXPECT_EQ(results["still"], "attack");
    EXPECT_EQ(results["walking"], "run");
}

// Items fall onto their spot, bounce once and wait on the ground until the knight reaches them: meat fills his health, a pouch holds gold, an emblem turns him into another kind of warrior with its reach, interval and strength, an archer looses his arrow on the frame he releases it, and an emblem of the kind he already is gives gold.
TEST_F(GameModuleTest, PicksUpWhatFallsAndBecomesAnotherKindOfWarrior) {
    const json results = run("task-hero", R"(
        local adventure = include("adventure")
        local golds = 0
        local function steps(state, seconds)
            local events = {}
            for _ = 1, math.floor(seconds / 0.05 + 0.5) do
                for _, event in ipairs(adventure.step(state, 0.05, 1)) do
                    golds = golds + (event == "gold" and 1 or 0)
                    events[#events + 1] = event ~= "gold" and event or nil
                end
            end
            return table.concat(events, ",")
        end

        local state = adventure.new({ gold = 0, level = 1, experience = 0, victories = 0, class = "warrior" }, random)
        adventure.resize(state, 600)
        local hero = adventure.heroX()
        local function faraway()
            state.foes = { { kind = "sheep", x = 5000, health = 12, most = 12, damage = 0, action = "idle", swing = 0, cooldown = 0 } }
        end

        faraway()
        adventure.drop(state, "meat", hero + 300)
        local item = state.items[1]
        steps(state, 0.2)
        local falling = { resting = item.resting, below = item.y > -12 }
        local landed = false
        local bounced = false
        for _ = 1, 40 do
            steps(state, 0.05)
            landed = landed or item.y == adventure.ground
            bounced = bounced or (item.bounced and item.velocity < 0)
        end
        local waiting = { resting = item.resting, items = #state.items, ahead = item.x > hero }
        state.hero.health = 30
        local picked = ""
        for _ = 1, 200 do
            picked = picked .. steps(state, 0.05)
            if #state.items == 0 then
                break
            end
        end
        local healed = state.hero.health

        faraway()
        adventure.drop(state, "pouch", hero)
        steps(state, 3.5)
        local pouch = state.progress.gold

        faraway()
        adventure.drop(state, "emblem", hero)
        state.items[1].class = "lancer"
        local changed = steps(state, 2)
        local lancer = adventure.fight(state)

        faraway()
        adventure.drop(state, "emblem", hero)
        state.items[1].class = "lancer"
        golds = 0
        local same = steps(state, 3.5)
        local repeated = state.progress.gold - pouch
        local told = golds

        faraway()
        adventure.drop(state, "emblem", hero)
        state.items[1].class = "archer"
        steps(state, 2)
        local archer = adventure.fight(state)
        state.random = function() return 0.99 end
        state.foes = { { kind = "red-warrior", x = hero + 100, health = 1000, most = 1000, damage = 1, action = "idle", swing = 0, cooldown = 0 }, { kind = "sheep", x = 5000, health = 12, most = 12, damage = 0, action = "idle", swing = 0, cooldown = 0 } }
        adventure.step(state, 0.05, 1)
        local drawing = { action = state.hero.action, shots = #state.shots }
        steps(state, 0.35)
        local released = #state.shots
        steps(state, 0.5)

        host.test_result({ falling = falling, landed = landed, bounced = bounced, waiting = waiting, picked = picked, healed = healed, pouch = pouch, changed = changed, lancer = lancer, same = same, repeated = repeated, told = told, class = state.progress.class, archer = archer, drawing = drawing, released = released, wounded = state.foes[1].health })
    )");

    EXPECT_FALSE(results["falling"]["resting"]);
    EXPECT_TRUE(results["falling"]["below"]);
    EXPECT_TRUE(results["landed"]);
    EXPECT_TRUE(results["bounced"]);
    EXPECT_TRUE(results["waiting"]["resting"]);
    EXPECT_EQ(results["waiting"]["items"], 1);
    EXPECT_TRUE(results["waiting"]["ahead"]);
    EXPECT_EQ(results["picked"], "item");
    EXPECT_EQ(results["healed"], 60);
    EXPECT_EQ(results["pouch"], 8);
    EXPECT_EQ(results["changed"], "class,item");
    EXPECT_EQ(results["lancer"]["reach"], 46);
    EXPECT_DOUBLE_EQ(results["lancer"]["interval"].get<double>(), 1.1);
    EXPECT_EQ(results["lancer"]["damage"], 12);
    EXPECT_EQ(results["same"], "item");
    EXPECT_EQ(results["repeated"], 10);
    EXPECT_GE(results["told"], 1);
    EXPECT_EQ(results["class"], "archer");
    EXPECT_EQ(results["archer"]["reach"], 120);
    EXPECT_TRUE(results["archer"]["shoots"]);
    EXPECT_EQ(results["drawing"]["action"], "attack");
    EXPECT_EQ(results["drawing"]["shots"], 0);
    EXPECT_EQ(results["released"], 1);
    EXPECT_EQ(results["wounded"], 1000 - 6);
}

// An arrow rises from the bow and comes down on the spot its target stood on when it was loosed, even after the target walked on, turning up as it leaves and down as it lands, and one flying to the left is drawn mirrored.
TEST_F(GameModuleTest, LoosesEachArrowInAnArcOnWhereItsTargetStood) {
    const json results = run("task-hero", R"(
        local adventure = include("adventure")
        local drawing = include("drawing")
        local sheets = include("sheets")
        local hero = adventure.heroX()
        local blocker = { kind = "sheep", x = 5000, health = 12, most = 12, damage = 0, action = "idle", swing = 0, cooldown = 0 }
        local function fresh(class)
            local state = adventure.new({ gold = 0, level = 1, experience = 0, victories = 0, class = class }, random)
            adventure.resize(state, 600)
            state.random = function() return 0.99 end
            return state
        end
        local function drawn(state)
            for _, command in ipairs(drawing.commands(state, { width = 600, height = 96 }, false)) do
                if command.image == sheets.arrow then
                    return { flipX = command.flipX, rotation = command.rotation }
                end
            end
        end

        local state = fresh("archer")
        local foe = { kind = "red-warrior", x = hero + 100, health = 1000, most = 1000, damage = 1, action = "run", swing = 0, cooldown = 1 }
        state.foes = { foe, blocker }
        state.hero.action = "attack"
        state.hero.swing = 0
        state.hero.blow = { at = 0, target = foe, damage = 10, shoots = true, from = hero + 8 }
        adventure.step(state, 0.01, 1)
        local shot = state.shots[1]
        local loosed = { to = shot.to, flight = shot.flight, angle = shot.angle, drawn = drawn(state) }
        local highest = shot.y
        for _ = 1, 100 do
            if #state.shots == 0 then
                break
            end
            adventure.step(state, 0.01, 1)
            highest = math.min(highest, shot.y)
        end
        local landed = { x = shot.x, y = shot.y, angle = shot.angle, foe = foe.x, health = foe.health }

        local defended = fresh("warrior")
        local archer = { kind = "purple-archer", x = hero + 110, health = 24, most = 24, damage = 7, action = "attack", swing = 0, cooldown = 1, blow = { at = 0, damage = 7, shoots = true, from = hero + 102 } }
        defended.foes = { archer, blocker }
        adventure.step(defended, 0.01, 1)
        local arrow = defended.arrows[1]
        local incoming = { to = arrow.to, from = arrow.from, angle = arrow.angle, drawn = drawn(defended) }
        for _ = 1, 100 do
            if #defended.arrows == 0 then
                break
            end
            adventure.step(defended, 0.01, 1)
        end

        host.test_result({ hero = hero, ground = adventure.ground, loosed = loosed, highest = highest, landed = landed, incoming = incoming, hit = adventure.health(1) - defended.hero.health })
    )");

    const double hero = results["hero"].get<double>();
    const double bow = results["ground"].get<double>() - 17.0;
    EXPECT_NEAR(results["loosed"]["to"].get<double>(), hero + 100.0, 1e-9);
    EXPECT_NEAR(results["loosed"]["flight"].get<double>(), 92.0 / 400.0, 1e-9);
    EXPECT_LT(results["loosed"]["angle"].get<double>(), 0.0);
    EXPECT_FALSE(results["loosed"]["drawn"]["flipX"]);
    EXPECT_LT(results["loosed"]["drawn"]["rotation"].get<double>(), 0.0);
    EXPECT_NEAR(results["highest"].get<double>(), bow - 23.0, 0.1);
    EXPECT_NEAR(results["landed"]["x"].get<double>(), hero + 100.0, 1e-9);
    EXPECT_NEAR(results["landed"]["y"].get<double>(), bow, 1e-9);
    EXPECT_GT(results["landed"]["angle"].get<double>(), 0.0);
    EXPECT_LT(results["landed"]["foe"].get<double>(), hero + 95.0);
    EXPECT_EQ(results["landed"]["health"], 990);

    EXPECT_NEAR(results["incoming"]["to"].get<double>(), hero, 1e-9);
    EXPECT_NEAR(results["incoming"]["from"].get<double>(), hero + 102.0, 1e-9);
    EXPECT_LT(results["incoming"]["angle"].get<double>(), -std::numbers::pi / 2.0);
    EXPECT_TRUE(results["incoming"]["drawn"]["flipX"]);
    EXPECT_GT(results["incoming"]["drawn"]["rotation"].get<double>(), 0.0);
    EXPECT_LT(results["incoming"]["drawn"]["rotation"].get<double>(), std::numbers::pi / 2.0);
    EXPECT_EQ(results["hit"], 7);
}

// Every pose of every figure stands on the middle and the bottom of the shadow its sheet draws, and a foe drawn mirrored stands on it too, so turning from one pose to another never moves a figure.
TEST_F(GameModuleTest, StandsEveryPoseOfAFigureOnItsShadow) {
    const json results = run("task-hero", R"(
        local adventure = include("adventure")
        local drawing = include("drawing")
        local sheets = include("sheets")
        local poses = {}
        local function add(look)
            for _, key in ipairs({ "idle", "run", "attack", "second", "guard" }) do
                local sheet = look[key]
                if sheet ~= nil then
                    poses[#poses + 1] = { image = sheet.image, width = sheet.width, frames = sheet.frames, centre = sheet.centre, feet = sheet.feet }
                end
            end
        end
        for _, name in ipairs(sheets.classNames) do
            add(sheets.classes[name])
        end
        for kind, look in pairs(sheets.foes) do
            if kind ~= "sheep" then
                add(look)
            end
        end

        local mirrored = {}
        for kind, look in pairs(sheets.foes) do
            for _, action in ipairs({ "idle", "run", "attack" }) do
                local sheet = look[action]
                if kind ~= "sheep" and sheet ~= nil then
                    local state = adventure.new({ gold = 0, level = 1, experience = 0, victories = 0, class = "warrior" }, random)
                    adventure.resize(state, 600)
                    state.foes = { { kind = kind, x = 300, health = 10, most = 10, action = action, swing = 0, cooldown = 0 } }
                    for _, command in ipairs(drawing.commands(state, { width = 600, height = 96 }, false)) do
                        if command.image == sheet.image then
                            mirrored[#mirrored + 1] = { image = sheet.image, width = sheet.width, frames = sheet.frames, x = command.x, size = command.width, flip = command.flipX == true }
                        end
                    end
                end
            end
        end
        host.test_result({ poses = poses, mirrored = mirrored, place = 300 })
    )");

    const std::filesystem::path assets = Resources::staged() / "plugins" / "task-hero" / "assets";
    ASSERT_GT(results["poses"].size(), 10U);

    for (const auto& pose : results["poses"]) {
        const std::string image = pose["image"];
        const Shadow found = shadow(assets / image, pose["width"].get<int>(), pose["frames"].get<int>());
        ASSERT_GT(found.bottom, 0) << image;
        EXPECT_NEAR(found.centre, pose["centre"].get<double>(), 5.0) << image;
        EXPECT_NEAR(static_cast<double>(found.bottom), pose["feet"].get<double>(), 1.0) << image;
    }

    // A foe faces the knight mirrored, so its shadow lands as far from the right side of its rectangle as it stands from the left side of the picture.
    ASSERT_EQ(results["mirrored"].size(), 12U);

    for (const auto& pose : results["mirrored"]) {
        const std::string image = pose["image"];
        const Shadow found = shadow(assets / image, pose["width"].get<int>(), pose["frames"].get<int>());
        const double width = pose["width"].get<double>();
        const double scale = pose["size"].get<double>() / width;
        ASSERT_TRUE(pose["flip"].get<bool>()) << image;
        EXPECT_NEAR(pose["x"].get<double>() + (width - found.centre) * scale, results["place"].get<double>(), 5.0 * scale) << image;
    }
}

// An item falls only after the knight walked far enough since the last one, never while he stands and fights, at most two lie ahead at once each a good gap from the other, and whatever wholly leaves the band on the left, foes, items, scenery, clouds and the smoke of a beaten foe, leaves the adventure.
TEST_F(GameModuleTest, SpacesWhatFallsAndForgetsWhatLeavesOnTheLeft) {
    const json results = run("task-hero", R"(
        local adventure = include("adventure")
        local sheets = include("sheets")
        local function steps(state, seconds)
            for _ = 1, math.floor(seconds / 0.05 + 0.5) do
                adventure.step(state, 0.05, 1)
            end
        end
        local function faraway(state)
            state.foes = { { kind = "sheep", x = 5000, health = 12, most = 12, damage = 0, action = "idle", swing = 0, cooldown = 0 } }
        end

        local state = adventure.new({ gold = 0, level = 1, experience = 0, victories = 0, class = "warrior" }, random)
        adventure.resize(state, 900)
        faraway(state)
        state.supply = 0
        adventure.step(state, 0.05, 1)
        local first = #state.items

        -- The knight stands still while he rests, so a supply that falls due waits for him to walk on.
        state.supply = 0
        state.hero.resting = 3
        steps(state, 2.5)
        local standing = #state.items
        steps(state, 8)
        local walked = #state.items
        local gap = #state.items == 2 and math.abs(state.items[2].x - state.items[1].x) or 0

        state.supply = 0
        steps(state, 8)
        local most = #state.items

        local narrow = adventure.new({ gold = 0, level = 1, experience = 0, victories = 0, class = "warrior" }, random)
        adventure.resize(narrow, 400)
        adventure.drop(narrow, "pouch", 330)
        local full = adventure.landing(narrow) == nil

        -- A long walk leaves only what can still be seen, and the smoke of a beaten foe moves with the ground.
        local long = adventure.new({ gold = 0, level = 1, experience = 0, victories = 0, class = "warrior" }, random)
        adventure.resize(long, 600)
        faraway(long)
        long.foes[1].x = 100000
        long.supply = math.huge
        long.foes[2] = { kind = "red-warrior", x = adventure.heroX() - 20, health = 10, most = 10, damage = 0, action = "idle", swing = 0, cooldown = 0 }
        long.items = { { kind = "pouch", x = adventure.heroX() - 20, y = adventure.ground, velocity = 0, bounced = true, resting = true } }
        long.effects = { { kind = "smoke", x = 300, y = 60, time = 0, grounded = true } }
        adventure.step(long, 0.05, 1)
        local drifted = long.effects[1] ~= nil and long.effects[1].x < 300
        steps(long, 120)
        local visible = true
        for _, piece in ipairs(long.scenery) do
            visible = visible and piece.x + sheets.span(sheets.scenery[piece.look]) * adventure.sceneryScale > 0
        end
        for _, cloud in ipairs(long.clouds) do
            visible = visible and cloud.x + cloud.look.width * adventure.cloudScale > 0
        end

        host.test_result({ first = first, standing = standing, walked = walked, gap = gap, most = most, full = full, drifted = drifted, visible = visible, foes = #long.foes, items = #long.items, scenery = #long.scenery, clouds = #long.clouds, shift = long.groundShift, tile = adventure.tile })
    )");

    EXPECT_EQ(results["first"], 1);
    EXPECT_EQ(results["standing"], 1);
    EXPECT_EQ(results["walked"], 2);
    EXPECT_GE(results["gap"].get<double>(), 72.0);
    EXPECT_EQ(results["most"], 2);
    EXPECT_TRUE(results["full"]);
    EXPECT_TRUE(results["drifted"]);
    EXPECT_TRUE(results["visible"]);
    EXPECT_EQ(results["foes"], 1);
    EXPECT_EQ(results["items"], 0);
    EXPECT_LE(results["scenery"], 30);
    EXPECT_LE(results["clouds"], 6);
    EXPECT_GE(results["shift"].get<double>(), 0.0);
    EXPECT_LT(results["shift"].get<double>(), results["tile"].get<double>());
}

// The progress of the knight is written at most once every ten seconds of adventure and at once when asked, and a progress set back drops what waited.
TEST_F(GameModuleTest, WritesTheProgressOfTheKnightAtMostOnceEveryTenSeconds) {
    const json result = run("task-hero", R"(
        local progress = include("progress")
        local writes = {}
        local keeper = progress.keeper(function(value)
            writes[#writes + 1] = value.victories
            return "written"
        end)
        local value = { victories = 0 }

        for tick = 1, 100 do
            value.victories = tick
            progress.changed(keeper, value)
            progress.tick(keeper, 0.25)
        end

        local ticked = #writes
        value.victories = 101
        progress.changed(keeper, value)
        local flushed = progress.flush(keeper)
        local idle = progress.flush(keeper)
        value.victories = 102
        progress.changed(keeper, value)
        progress.forget(keeper)
        local forgotten = progress.flush(keeper)
        host.test_result({ writes = writes, ticked = ticked, flushed = flushed, idle = idle == nil, forgotten = forgotten == nil })
    )");

    EXPECT_EQ(result["ticked"], 2);
    EXPECT_EQ(result["writes"], json::array({40, 80, 101}));
    EXPECT_EQ(result["flushed"], "written");
    EXPECT_EQ(result["idle"], true);
    EXPECT_EQ(result["forgotten"], true);
}

// The band is drawn from the sky to the counters, every figure from its sheet, the ground reaches the bottom of the band at every height, the scenery stands on a line behind the path of the figures, and the counters leave when the reader hides them.
// The night follows the clock from seven in the evening to seven in the morning, and brings stars, a moon, fainter clouds and a shade over the world, while the day keeps the scene of the pack and the bars, each framed in the dark outline of the pack, and the counters stay above the shade.
TEST_F(GameModuleTest, BringsTheNightByTheHourOfTheClock) {
    const json results = run("task-hero", R"(
        local adventure = include("adventure")
        local daylight = include("daylight")
        local drawing = include("drawing")
        local sheets = include("sheets")
        local state = adventure.new({ gold = 12, level = 3, experience = 5, victories = 4, class = "lancer" }, random)
        adventure.resize(state, 600)
        adventure.step(state, 0.1, 1)

        local function drawn(night)
            local found = { sky = false, stars = 0, moons = 0, outlines = 0, shade = 0, lastBar = 0, firstText = 0, strips = 0, clouds = {} }
            for index, command in ipairs(drawing.commands(state, { width = 600, height = 96 }, true, night)) do
                found.sky = found.sky or command.image == sheets.sky
                found.stars = found.stars + (command.color == "#fff4d6" and 1 or 0)
                found.moons = found.moons + (command.op == "circle" and command.radius == 5 and 1 or 0)
                found.outlines = found.outlines + (command.color == "#161c2e" and 1 or 0)
                found.strips = found.strips + (command.op == "rect" and command.height == 4 and command.width == 600 and 1 or 0)
                found.shade = command.color == "#060d24" and index or found.shade
                found.lastBar = command.color == "#161c2e" and index or found.lastBar
                found.firstText = (command.op == "text" and found.firstText == 0) and index or found.firstText
                if command.image ~= nil and command.image:find("cloud") then
                    found.clouds[#found.clouds + 1] = command.opacity
                end
            end
            return found
        end

        local hours = {}
        for _, hour in ipairs({ 0, 3, 6.99, 7, 12, 18.99, 19, 23.5, 24 }) do
            hours[#hours + 1] = daylight.night(hour) ~= nil
        end

        host.test_result({ hours = hours, seconds = daylight.night(19.5).seconds, day = drawn(nil), night = drawn(daylight.night(1)) })
    )");

    EXPECT_EQ(results["hours"], json::array({true, true, true, false, false, false, true, true, true}));
    EXPECT_DOUBLE_EQ(results["seconds"].get<double>(), 19.5 * 3600.0);

    EXPECT_TRUE(results["day"]["sky"]);
    EXPECT_EQ(results["day"]["strips"], 0);
    EXPECT_EQ(results["day"]["stars"], 0);
    EXPECT_EQ(results["day"]["moons"], 0);
    EXPECT_EQ(results["day"]["shade"], 0);
    EXPECT_EQ(results["day"]["outlines"], 3);

    for (const auto& opacity : results["day"]["clouds"]) {
        EXPECT_DOUBLE_EQ(opacity.get<double>(), 0.9);
    }

    EXPECT_FALSE(results["night"]["sky"]);
    EXPECT_EQ(results["night"]["strips"], 19);
    EXPECT_GT(results["night"]["stars"], 10);
    EXPECT_EQ(results["night"]["moons"], 1);
    EXPECT_GT(results["night"]["shade"], 0);
    EXPECT_GT(results["night"]["lastBar"], results["night"]["shade"]);
    EXPECT_GT(results["night"]["firstText"], results["night"]["shade"]);
    EXPECT_EQ(results["night"]["outlines"], 3);

    for (const auto& opacity : results["night"]["clouds"]) {
        EXPECT_DOUBLE_EQ(opacity.get<double>(), 0.45);
    }
}

// The counters keep a gap between the emblem, the coin, the gold and the level whatever their texts measure, and their box widens to hold them.
TEST_F(GameModuleTest, SpacesTheCountersAndWidensTheirBox) {
    const json results = run("task-hero", R"(
        local adventure = include("adventure")
        local drawing = include("drawing")
        local laid = {}

        for index, progress in ipairs({ { gold = 12, level = 3 }, { gold = 1234567, level = 99 } }) do
            local state = adventure.new({ gold = progress.gold, level = progress.level, experience = 0, victories = 0, class = "warrior" }, random)
            adventure.resize(state, 600)
            local counters = drawing.counters(state, 1)
            laid[index] = { left = counters.left, right = counters.right, width = counters.width, emblem = counters.emblem, coin = counters.coin, goldAt = counters.goldAt, goldEnd = counters.goldAt + counters.goldWidth, levelStart = counters.levelAt - counters.levelWidth, levelAt = counters.levelAt }
        end

        host.test_result({ laid = laid })
    )");

    for (const auto& counters : results["laid"]) {
        EXPECT_GT(counters["emblem"].get<double>(), counters["left"].get<double>());
        EXPECT_GE(counters["coin"].get<double>() - counters["emblem"].get<double>(), 14.0 + 10.0);
        EXPECT_GE(counters["levelStart"].get<double>() - counters["goldEnd"].get<double>(), 10.0 - 1e-9);
        EXPECT_LT(counters["levelAt"].get<double>(), counters["right"].get<double>());
    }

    EXPECT_GE(results["laid"][0]["width"].get<double>(), 128.0);
    EXPECT_GT(results["laid"][1]["width"].get<double>(), results["laid"][0]["width"].get<double>());
    EXPECT_DOUBLE_EQ(results["laid"][1]["right"].get<double>(), results["laid"][0]["right"].get<double>());
}

TEST_F(GameModuleTest, DrawsTheAdventureAcrossItsBand) {
    const json results = run("task-hero", R"(
        local adventure = include("adventure")
        local drawing = include("drawing")
        local sheets = include("sheets")
        local state = adventure.new({ gold = 12, level = 3, experience = 5, victories = 4, class = "lancer" }, random)
        adventure.resize(state, 600)
        adventure.step(state, 0.1, 1)
        local function count(commands)
            local images, texts, framed = 0, 0, 0
            for _, command in ipairs(commands) do
                images = images + (command.op == "image" and 1 or 0)
                texts = texts + (command.op == "text" and 1 or 0)
                framed = framed + (command.source ~= nil and 1 or 0)
            end
            return { images = images, texts = texts, framed = framed }
        end
        local bottoms = {}
        for _, height in ipairs({ 64, 88, 120 }) do
            local lowest = 0
            for _, command in ipairs(drawing.commands(state, { width = 900, height = height }, true)) do
                if command.image == "terrain/tilemap.png" then
                    lowest = math.max(lowest, command.y + command.height)
                end
            end
            bottoms[#bottoms + 1] = lowest - height
        end
        local scenery = {}
        for _, sheet in ipairs(sheets.scenery) do
            scenery[sheet.image] = sheet
        end
        local lines = {}
        local emblem = false
        for _, command in ipairs(drawing.commands(state, { width = 600, height = 96 }, true)) do
            emblem = emblem or command.image == "loot/emblem-lancer.png"
            local sheet = scenery[command.image]
            if sheet ~= nil then
                lines[#lines + 1] = command.y + sheet.feet * command.height / sheet.height
            end
        end
        host.test_result({ shown = count(drawing.commands(state, { width = 600, height = 96 }, true)), hidden = count(drawing.commands(state, { width = 600, height = 96 }, false)), bottoms = bottoms, emblem = emblem, lines = lines, ground = adventure.ground })
    )");

    EXPECT_GT(results["shown"]["images"], 10);
    EXPECT_GT(results["shown"]["framed"], 5);
    EXPECT_EQ(results["shown"]["texts"], 2);
    EXPECT_EQ(results["hidden"]["texts"], 0);
    EXPECT_EQ(results["shown"]["images"].get<int>() - results["hidden"]["images"].get<int>(), 2);
    EXPECT_TRUE(results["emblem"]);

    for (const auto& bottom : results["bottoms"]) {
        EXPECT_NEAR(bottom.get<double>(), 0.0, 1e-9);
    }

    ASSERT_FALSE(results["lines"].empty());

    for (const auto& line : results["lines"]) {
        EXPECT_NEAR(line.get<double>(), results["ground"].get<double>() - 8.0, 1e-9);
    }
}

} // namespace workpane::tests
