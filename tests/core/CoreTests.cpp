#include "Result.h"
#include "app/ApplicationPaths.h"
#include "app/ApplicationPathsResolver.h"
#include "app/CommandLine.h"
#include "app/CorePreferences.h"
#include "app/FrameScheduler.h"
#include "app/GeometryRecorder.h"
#include "execution/MainThreadQueue.h"
#include "execution/WorkerPool.h"
#include "files/DirectoryEntry.h"
#include "files/DirectoryListing.h"
#include "files/TextSearch.h"
#include "files/TreeWalk.h"
#include "json/ObjectReader.h"
#include "logging/LogEntry.h"
#include "logging/LogLevels.h"
#include "logging/LogService.h"
#include "platform/WindowGeometry.h"
#include "process/ProcessEnvironment.h"
#include "process/ProcessLaunch.h"
#include "process/ProcessText.h"
#include "support/TemporaryDirectory.h"
#include "text/Base64.h"
#include "text/Integers.h"
#include "text/Utf8.h"
#include "time/Timestamps.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace workpane {

TEST(Result, CarriesEitherAValueOrAnError) {
    const auto success = Result<int>::success(7);
    const auto failure = Result<int>::failure({"code", "message", "detail"});

    ASSERT_TRUE(success.hasValue());
    EXPECT_EQ(success.value(), 7);
    ASSERT_FALSE(failure.hasValue());
    EXPECT_EQ(failure.error().code, "code");
    EXPECT_EQ(failure.error().detail, "detail");
    EXPECT_TRUE(Result<void>::success().hasValue());
    EXPECT_FALSE(Result<void>::failure({"code", "message", ""}).hasValue());
}

TEST(MainThreadQueue, RunsPostedTasksInOrderAndWakesTheLoop) {
    execution::MainThreadQueue queue;
    std::vector<int> order;
    int wakes = 0;
    // clang-format off
    queue.setWakeHandler([&wakes]() { ++wakes; });
    queue.post([&order]() { order.push_back(1); });
    queue.post([&order]() { order.push_back(2); });
    // clang-format on

    EXPECT_FALSE(queue.empty());
    EXPECT_EQ(wakes, 2);
    queue.drain();
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
    EXPECT_TRUE(queue.empty());
    queue.drain();
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
}

TEST(WorkerPool, RunsEveryTaskBeforeItShutsDown) {
    std::atomic<int> done{0};
    execution::WorkerPool pool(3);

    for (int index = 0; index < 50; ++index) {
        // clang-format off
        pool.post([&done]() { ++done; });
        // clang-format on
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    while (done.load() < 50 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    pool.shutdown();
    EXPECT_EQ(done.load(), 50);
}

// A number the system wrote is read whole or not at all, in decimal or hexadecimal, and text that is no number answers nothing instead of ending the product.
TEST(Integers, ReadsTheNumbersTheSystemWritesWithoutThrowing) {
    EXPECT_EQ(text::Integers::parse("2400000\n"), 2400000);
    EXPECT_EQ(text::Integers::parse("  42 "), 42);
    EXPECT_EQ(text::Integers::parse("0x10de", 16), 0x10DE);
    EXPECT_EQ(text::Integers::parse("D0C", 16), 0xD0C);
    EXPECT_FALSE(text::Integers::parse("").has_value());
    EXPECT_FALSE(text::Integers::parse("12abc").has_value());
    EXPECT_FALSE(text::Integers::parse("unknown").has_value());
    EXPECT_FALSE(text::Integers::parse("99999999999999999999999").has_value());
    EXPECT_EQ(text::Integers::leading("       16384 kB"), 16384);
    EXPECT_FALSE(text::Integers::leading(" kB").has_value());
}

TEST(ObjectReader, ReadsDeclaredFieldsAndRefusesUnknownOnes) {
    const json::Json value = {{"name", "logs"}, {"count", 3}, {"ratio", 0.5}, {"enabled", true}, {"extra", 1}};
    std::string name;
    std::int64_t count = 0;
    double ratio = 0.0;
    bool enabled = false;
    json::ObjectReader reader(value, "sample");
    reader.readText("name", name).readInteger("count", count, 0, 10).readNumber("ratio", ratio, 0.0, 1.0).read("enabled", enabled);
    const auto finished = reader.finish();

    EXPECT_EQ(name, "logs");
    EXPECT_EQ(count, 3);
    EXPECT_DOUBLE_EQ(ratio, 0.5);
    EXPECT_TRUE(enabled);
    ASSERT_FALSE(finished.hasValue());
    EXPECT_EQ(finished.error().code, "json_field_unknown");
    EXPECT_EQ(finished.error().detail, "sample.extra");

    // A field read twice counts once, so the unknown field beside it is still found, and an object read whole is accepted.
    json::ObjectReader twice(value, "sample");
    twice.readText("name", name).readText("name", name).readInteger("count", count, 0, 10).readNumber("ratio", ratio, 0.0, 1.0).read("enabled", enabled);
    EXPECT_EQ(twice.finish().error().detail, "sample.extra");
    std::int64_t extra = 0;
    json::ObjectReader whole(value, "sample");
    whole.readText("name", name).readInteger("count", count, 0, 10).readNumber("ratio", ratio, 0.0, 1.0).read("enabled", enabled).readInteger("extra", extra, 0, 1).readInteger("extra", extra, 0, 1);
    EXPECT_TRUE(whole.finish().hasValue());
}

TEST(ObjectReader, RefusesMissingEmptyWrongAndOutOfRangeValues) {
    // clang-format off
    const auto failureOf = [](const json::Json& value) {
        std::string name;
        std::int64_t count = 0;
        json::ObjectReader reader(value, "sample");
        reader.readText("name", name).readInteger("count", count, 0, 10);

        return reader.finish();
    };
    // clang-format on

    EXPECT_EQ(failureOf({{"count", 1}}).error().code, "json_field_missing");
    EXPECT_EQ(failureOf({{"name", ""}, {"count", 1}}).error().code, "json_field_empty");
    EXPECT_EQ(failureOf({{"name", 5}, {"count", 1}}).error().code, "json_field_type");
    EXPECT_EQ(failureOf({{"name", "a"}, {"count", 11}}).error().code, "json_field_range");
    EXPECT_TRUE(failureOf({{"name", "a"}, {"count", 10}}).hasValue());

    // A whole number written as a real, as Lua writes half of an even height, reads as the integer it is, while a fraction and a real beyond the signed range are refused.
    EXPECT_TRUE(failureOf({{"name", "a"}, {"count", 4.0}}).hasValue());
    EXPECT_EQ(failureOf({{"name", "a"}, {"count", 4.5}}).error().code, "json_field_type");
    EXPECT_EQ(failureOf({{"name", "a"}, {"count", 1e300}}).error().code, "json_field_range");
    EXPECT_EQ(failureOf({{"name", "a"}, {"count", -1e300}}).error().code, "json_field_range");
}

TEST(ObjectReader, ReadsClosedChoicesAndOptionalFields) {
    enum class Side { Left, Right };
    Side side = Side::Left;
    std::string absent = "kept";
    const json::Json value = {{"side", "right"}};
    json::ObjectReader reader(value, "sample");
    reader.readChoice("side", side, {{"left", Side::Left}, {"right", Side::Right}}).readText("absent", absent, json::Presence::Optional);

    EXPECT_TRUE(reader.finish().hasValue());
    EXPECT_EQ(side, Side::Right);
    EXPECT_EQ(absent, "kept");

    const json::Json up = {{"side", "up"}};
    json::ObjectReader outside(up, "sample");
    outside.readChoice("side", side, {{"left", Side::Left}, {"right", Side::Right}});
    EXPECT_EQ(outside.finish().error().code, "json_field_choice");
}

TEST(ObjectReader, TreatsAnEmptyObjectAsAnEmptyList) {
    EXPECT_TRUE(json::ObjectReader::isList(json::Json::array()));
    EXPECT_TRUE(json::ObjectReader::isList(json::Json::object()));
    EXPECT_FALSE(json::ObjectReader::isList({{"key", 1}}));
    EXPECT_FALSE(json::ObjectReader::isList("text"));

    // A list read from an empty object is the shared empty list, and a field that is absent keeps what the caller gave.
    const json::Json value = {{"items", json::Json::object()}};
    const json::Json* items = nullptr;
    const json::Json* extra = &json::ObjectReader::absent();
    json::ObjectReader reader(value, "sample");
    reader.readArray("items", items).readAny("extra", extra, json::Presence::Optional);
    ASSERT_TRUE(reader.finish().hasValue());
    EXPECT_EQ(items, &json::ObjectReader::emptyList());
    EXPECT_TRUE(extra->is_null());
}

// An object, a list or any value is answered where it stands inside what was read, so reading a tree never copies it.
TEST(ObjectReader, AnswersStructuredFieldsWithoutCopyingThem) {
    const json::Json value = {{"tree", {{"children", json::Json::array({1, 2, 3})}}}, {"items", json::Json::array({"a"})}, {"any", 7}};
    const json::Json* tree = nullptr;
    const json::Json* items = nullptr;
    const json::Json* any = nullptr;
    json::ObjectReader reader(value, "sample");
    reader.readObject("tree", tree).readArray("items", items).readAny("any", any);

    ASSERT_TRUE(reader.finish().hasValue());
    EXPECT_EQ(tree, &value["tree"]);
    EXPECT_EQ(items, &value["items"]);
    EXPECT_EQ(any, &value["any"]);
}

TEST(Timestamps, WritesAndReadsStoredMomentsInUtcWithMilliseconds) {
    const time::Instant instant{std::chrono::milliseconds(1790000000123)};
    const std::string stored = time::Timestamps::storedTimestamp(instant);

    EXPECT_EQ(stored, "2026-09-21T14:13:20.123Z");
    ASSERT_TRUE(time::Timestamps::parseStoredTimestamp(stored).has_value());
    EXPECT_EQ(*time::Timestamps::parseStoredTimestamp(stored), instant);
    EXPECT_FALSE(time::Timestamps::parseStoredTimestamp("2026-09-21 13:33:20").has_value());
    EXPECT_FALSE(time::Timestamps::parseStoredTimestamp("2026-02-30T00:00:00.000Z").has_value());
    EXPECT_TRUE(time::Timestamps::localPresentation(instant).has_value());

    // A moment beyond the reach of a clock that counts nanoseconds, as the one of Linux does, converts to local time whole within the calendar every platform keeps, which ends with the year 3000 on Windows.
    const auto far = time::Timestamps::parseStoredTimestamp("2999-06-15T12:00:00.000Z");
    ASSERT_TRUE(far.has_value());
    const auto moment = time::Timestamps::localMoment(*far);
    ASSERT_TRUE(moment.has_value());
    EXPECT_EQ(moment->year, 2999);
}

// The core preferences C++ reads before Lua runs follow the rules the core settings declare, and a value that breaks them reads as empty.
TEST(CorePreferences, ReadsTheCoreKeysByTheRulesOfTheCoreSettings) {
    const std::string absolute = std::filesystem::path(std::filesystem::current_path().root_path() / "plugins").string();
    const app::CorePreferences read(nlohmann::json{{"language", "pt"}, {"theme", "blue"}, {"pluginFolders", {absolute}}});

    EXPECT_EQ(read.language(), "pt");
    EXPECT_EQ(read.theme(), "blue");
    EXPECT_EQ(read.pluginFolders(), (std::vector<std::filesystem::path>{absolute}));
    EXPECT_EQ(app::CorePreferences(nlohmann::json{{"language", 1}}).language(), "");
    EXPECT_TRUE(app::CorePreferences(nlohmann::json{{"pluginFolders", {absolute, "relative"}}}).pluginFolders().empty());
    EXPECT_TRUE(app::CorePreferences(nlohmann::json{{"pluginFolders", {absolute, absolute}}}).pluginFolders().empty());
    EXPECT_TRUE(app::CorePreferences(nlohmann::json{{"pluginFolders", nlohmann::json::object()}}).pluginFolders().empty());
    EXPECT_TRUE(app::CorePreferences(nlohmann::json{{"pluginFolders", std::vector<std::string>(33, absolute)}}).pluginFolders().empty());
}

TEST(CommandLine, AcceptsOnlyAnAbsoluteDataDirectory) {
    const std::string absolute = std::filesystem::temp_directory_path().string();
    const auto parsed = app::CommandLine::parse({"--data-dir", absolute});

    ASSERT_TRUE(parsed.hasValue());
    EXPECT_EQ(parsed.value().dataDirectory, std::filesystem::path(absolute).lexically_normal());
    EXPECT_EQ(parsed.value().arguments().size(), 2U);
    EXPECT_TRUE(app::CommandLine::parse({}).hasValue());
    EXPECT_EQ(app::CommandLine::parse({"--data-dir", "relative"}).error().code, "data_directory_relative");
    EXPECT_EQ(app::CommandLine::parse({"--data-dir"}).error().code, "command_line_value_missing");
    EXPECT_EQ(app::CommandLine::parse({"--verbose"}).error().code, "command_line_unknown");
    EXPECT_EQ(app::CommandLine::parse({"--data-dir", absolute, "--data-dir", absolute}).error().code, "command_line_repeated");
}

// The data directory may follow an equals sign, the help and the version are asked for by name, and an empty joined value is refused.
TEST(CommandLine, ReadsTheHelpTheVersionAndAJoinedDataDirectory) {
    const std::string absolute = std::filesystem::temp_directory_path().string();
    const auto joined = app::CommandLine::parse({"--data-dir=" + absolute});
    ASSERT_TRUE(joined.hasValue());
    EXPECT_EQ(joined.value().dataDirectory, std::filesystem::path(absolute).lexically_normal());

    const auto asked = app::CommandLine::parse({"--help", "--version"});
    ASSERT_TRUE(asked.hasValue());
    EXPECT_TRUE(asked.value().help);
    EXPECT_TRUE(asked.value().version);
    EXPECT_TRUE(app::CommandLine::parse({"-h", "-v"}).value().help);
    EXPECT_EQ(app::CommandLine::parse({"--data-dir="}).error().code, "command_line_value_missing");
    EXPECT_EQ(app::CommandLine::parse({"--data-dir=relative"}).error().code, "data_directory_relative");
}

// The help and the refusals speak the language they are given.
TEST(CommandLine, SpeaksTheLanguageOfTheSystem) {
    EXPECT_NE(app::CommandLine::usage("en").find("--data-dir <path>"), std::string::npos);
    EXPECT_NE(app::CommandLine::usage("pt").find("--data-dir <caminho>"), std::string::npos);

    const auto refused = app::CommandLine::parse({"--verbose"});
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(app::CommandLine::refusal(refused.error(), "en"), "The option \"--verbose\" is not one Workpane accepts");
    EXPECT_EQ(app::CommandLine::refusal(refused.error(), "pt"), "A opção \"--verbose\" não é uma que o Workpane aceita");
}

// A move is written once the window has held its new place for a moment, every step of a drag starts the wait again, and coming back to the written place writes nothing.
TEST(GeometryRecorder, WritesAPlaceOnceItSettles) {
    app::GeometryRecorder recorder;
    const platform::WindowGeometry written{10, 20, 1280, 800, false};
    recorder.start(written);
    EXPECT_TRUE(std::isinf(recorder.waitSeconds(0.0)));

    recorder.observe({30, 20, 1280, 800, false}, 1.0);
    recorder.observe({50, 20, 1280, 800, false}, 1.2);
    EXPECT_FALSE(recorder.due(1.5).has_value());
    EXPECT_NEAR(recorder.waitSeconds(1.5), 0.2, 1e-9);

    const auto settled = recorder.due(1.7);
    ASSERT_TRUE(settled.has_value());
    EXPECT_EQ(settled->x, 50);
    EXPECT_FALSE(recorder.due(2.0).has_value());

    recorder.observe({50, 20, 1440, 900, true}, 3.0);
    recorder.observe({50, 20, 1280, 800, false}, 3.1);
    EXPECT_FALSE(recorder.due(4.0).has_value());

    // A window that closes before its last place settled still writes it, once.
    recorder.observe({70, 20, 1280, 800, false}, 5.0);
    const auto closing = recorder.unwritten();
    ASSERT_TRUE(closing.has_value());
    EXPECT_EQ(closing->x, 70);
    EXPECT_FALSE(recorder.unwritten().has_value());
    EXPECT_FALSE(recorder.due(6.0).has_value());
}

TEST(ApplicationPaths, FindsTheResourcesOfEveryLayout) {
    const std::filesystem::path bundle = std::filesystem::path("/Applications/Workpane.app/Contents/MacOS/Workpane");
    const std::filesystem::path installed = std::filesystem::path("/opt/workpane/bin/Workpane");

    EXPECT_EQ(app::ApplicationPathsResolver::resourcesFor(bundle), std::filesystem::path("/Applications/Workpane.app/Contents/Resources"));
    EXPECT_EQ(app::ApplicationPathsResolver::resourcesFor(installed), std::filesystem::path("/opt/workpane/share/workpane"));

    const app::ApplicationPaths paths{installed, "/opt/workpane/share/workpane", "/data"};
    EXPECT_EQ(paths.lua(), std::filesystem::path("/opt/workpane/share/workpane/lua"));
    EXPECT_EQ(paths.plugins(), std::filesystem::path("/opt/workpane/share/workpane/plugins"));
    EXPECT_EQ(paths.fonts(), std::filesystem::path("/opt/workpane/share/workpane/fonts"));
}

TEST(FrameScheduler, SleepsWhenIdleAndDrawsForInputChangesAndDeadlines) {
    app::FrameScheduler scheduler;
    const double idle = std::numeric_limits<double>::infinity();

    // The first frames settle the window, and an idle window then sleeps until the system has an event.
    while (scheduler.shouldDraw({}, 0.0)) {
        scheduler.drew({});
    }

    EXPECT_TRUE(std::isinf(scheduler.waitSeconds(0.0, idle, false, false, true)));
    EXPECT_DOUBLE_EQ(scheduler.waitSeconds(0.0, 0.25, false, false, true), 0.25);
    EXPECT_DOUBLE_EQ(scheduler.waitSeconds(0.0, idle, true, false, true), app::FrameScheduler::dialogTickSeconds);
    EXPECT_DOUBLE_EQ(scheduler.waitSeconds(0.0, idle, false, true, true), 0.0);

    EXPECT_TRUE(scheduler.shouldDraw({true, false, false}, 1.0));
    scheduler.drew({});
    EXPECT_TRUE(scheduler.shouldDraw({false, false, false}, 1.0));
    scheduler.drew({});
    EXPECT_FALSE(scheduler.shouldDraw({false, false, false}, 1.0));

    // A change is followed by the same settling frames as input, so new content is laid out before the loop sleeps.
    EXPECT_TRUE(scheduler.shouldDraw({false, false, true}, 1.0));
    scheduler.drew({});
    EXPECT_TRUE(scheduler.shouldDraw({false, false, false}, 1.0));
    scheduler.drew({false, false, false, 5.0});
    EXPECT_FALSE(scheduler.shouldDraw({}, 4.0));
    EXPECT_DOUBLE_EQ(scheduler.waitSeconds(4.0, idle, false, false, true), 1.0);
    EXPECT_TRUE(scheduler.shouldDraw({}, 5.0));
}

// A window that is not shown, such as a minimized one, never wakes for its frames, its animations, its deadlines or work waiting to be drawn, while Lua, dialogs and native views still wake it.
// Input ImGui still holds in its queue after a frame keeps the frames coming without waiting for another event, and the loop sleeps once the queue is drawn.
TEST(FrameScheduler, DrawsAgainWhileInputWaitsInTheQueue) {
    app::FrameScheduler scheduler;
    const double idle = std::numeric_limits<double>::infinity();

    while (scheduler.shouldDraw({}, 0.0)) {
        scheduler.drew({});
    }

    for (int frame = 0; frame < 5; ++frame) {
        scheduler.drew({true, false, false});
        EXPECT_TRUE(scheduler.shouldDraw({}, 0.0)) << frame;
        EXPECT_DOUBLE_EQ(scheduler.waitSeconds(0.0, idle, false, false, true), 0.0) << frame;
    }

    scheduler.drew({});
    EXPECT_FALSE(scheduler.shouldDraw({}, 0.0));
}

TEST(FrameScheduler, WakesAHiddenWindowOnlyForWorkThatRunsWithoutDrawing) {
    app::FrameScheduler scheduler;
    const double idle = std::numeric_limits<double>::infinity();
    scheduler.drew({false, true, false, 0.5});

    EXPECT_DOUBLE_EQ(scheduler.waitSeconds(0.0, idle, false, false, true), 0.0);
    EXPECT_TRUE(std::isinf(scheduler.waitSeconds(0.0, idle, false, true, false)));
    EXPECT_DOUBLE_EQ(scheduler.waitSeconds(0.0, 0.25, false, true, false), 0.25);
    EXPECT_DOUBLE_EQ(scheduler.waitSeconds(0.0, idle, true, false, false), app::FrameScheduler::dialogTickSeconds);
}

// Entries wait until their consumer takes them, the consumer hears once per batch, and a cleared delivery is never called again.
TEST(LogService, KeepsEntriesUntilItsConsumerTakesThem) {
    logging::LogService logs;
    int notified = 0;
    // clang-format off
    const auto messages = [&logs]() {
        std::vector<std::string> taken;

        for (const auto& entry : logs.take()) {
            taken.push_back(entry.message);
        }

        return taken;
    };
    // clang-format on

    logs.write(logging::LogLevel::Info, "workpane", "test", "first");
    logs.write(logging::LogLevel::Error, "workpane", "test", "second", {{"code", "sample"}});
    // clang-format off
    logs.setDelivery([&notified]() { ++notified; });
    // clang-format on
    EXPECT_EQ(notified, 1);

    logs.write(logging::LogLevel::Warning, "workpane", "test", "third");
    EXPECT_EQ(notified, 1);
    EXPECT_EQ(messages(), (std::vector<std::string>{"first", "second", "third"}));
    EXPECT_TRUE(messages().empty());
    EXPECT_EQ(logging::LogLevels::parse("warning"), logging::LogLevel::Warning);
    EXPECT_EQ(logging::LogLevels::name(logging::LogLevel::Debug), "debug");
    EXPECT_FALSE(logging::LogLevels::parse("fatal").has_value());

    // A detail holding bytes that are not UTF-8, such as a file name on Linux, is written with replacements instead of ending the product.
    logs.write(logging::LogLevel::Error, "workpane", "test", "fourth", {{"path", std::string("/tmp/\xFF\xFE")}});
    EXPECT_EQ(notified, 2);
    EXPECT_EQ(messages(), std::vector<std::string>{"fourth"});

    // Entries written from many threads at once all reach the consumer, which is never called once its delivery is cleared.
    std::vector<std::thread> writers;

    for (int writer = 0; writer < 8; ++writer) {
        // clang-format off
        writers.emplace_back([&logs]() {
            for (int line = 0; line < 100; ++line) {
                logs.write(logging::LogLevel::Debug, "workpane", "test", "line");
            }
        });
        // clang-format on
    }

    for (auto& writer : writers) {
        writer.join();
    }

    EXPECT_EQ(messages().size(), 800U);
    logs.clearDelivery();
    const int cleared = notified;
    logs.write(logging::LogLevel::Info, "workpane", "test", "after");
    EXPECT_EQ(notified, cleared);
    EXPECT_EQ(messages(), std::vector<std::string>{"after"});
}

// Bytes a program writes become UTF-8 text, a character split between two reads waits for its end, and anything that is not UTF-8 becomes one replacement.
TEST(ProcessText, AssemblesUtf8AcrossReadsAndReplacesWhatIsNotUtf8) {
    process::ProcessText text;

    EXPECT_EQ(text.take("plain \xC3"), "plain ");
    EXPECT_EQ(text.take("\xA7\xE2\x82"), "\xC3\xA7");
    EXPECT_EQ(text.take("\xAC!"), "\xE2\x82\xAC!");
    EXPECT_EQ(text.take(std::string("\xFF\xC0\xAF") + "a"), std::string("\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD") + "a");
    EXPECT_EQ(text.take("\xED\xA0\x80"), "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD");
    EXPECT_EQ(text.take("\xF0\x9F"), "");
    EXPECT_EQ(text.finish(), "\xEF\xBF\xBD");
    EXPECT_EQ(text.finish(), "");
}

// The inherited environment loses the cleared variables and the replaced ones, and Windows compares their names without regard to case.
TEST(ProcessEnvironment, ClearsAndReplacesInheritedVariables) {
    const process::ProcessLaunch launch{{}, {}, {}, {{"PATH", "/opt/bin"}}, {"SECRET"}};
    const std::vector<std::string> inherited{"HOME=/home/reader", "Path=/usr/bin", "SECRET=1", "secret=2"};

    EXPECT_EQ(process::ProcessEnvironment::build(inherited, launch, false), (std::vector<std::string>{"HOME=/home/reader", "Path=/usr/bin", "secret=2", "PATH=/opt/bin"}));
    EXPECT_EQ(process::ProcessEnvironment::build(inherited, launch, true), (std::vector<std::string>{"HOME=/home/reader", "PATH=/opt/bin"}));
}

// A folder is listed with the kind of each entry, and a link is reported as a link rather than followed.
TEST(DirectoryListing, ListsEveryEntryWithItsKind) {
    tests::TemporaryDirectory folder;
    std::filesystem::create_directory(folder.path() / "src");
    std::ofstream(folder.path() / ".hidden") << "x";
    std::ofstream(folder.path() / "main.cpp") << "int main() {}";
    std::filesystem::create_directory_symlink(folder.path() / "src", folder.path() / "linked");
    auto listed = files::DirectoryListing::list(folder.path());
    ASSERT_TRUE(listed.hasValue());
    auto entries = listed.value();
    // clang-format off
    std::ranges::sort(entries, [](const files::DirectoryEntry& first, const files::DirectoryEntry& second) { return first.name < second.name; });
    // clang-format on

    ASSERT_EQ(entries.size(), 4U);
    EXPECT_EQ(entries[0].name, ".hidden");
    EXPECT_EQ(entries[1].name, "linked");
    EXPECT_EQ(entries[1].kind, files::DirectoryEntry::Kind::Symlink);
    EXPECT_EQ(entries[2].kind, files::DirectoryEntry::Kind::File);
    EXPECT_EQ(entries[2].size, 13U);
    EXPECT_EQ(entries[3].kind, files::DirectoryEntry::Kind::Directory);
    EXPECT_EQ(files::DirectoryListing::list(folder.path() / "missing").error().code, "files_directory_unavailable");
}

// A walk names every regular file relative to the root, hidden ones included, leaves out the named folders, linked folders and folders it cannot open, and says when it stopped at its bound.
TEST(TreeWalk, WalksTheFilesUnderAFolderUpToABound) {
    tests::TemporaryDirectory folder;
    const std::atomic<bool> running{false};
    std::filesystem::create_directories(folder.path() / "src" / "ui");
    std::filesystem::create_directories(folder.path() / ".git" / "objects");
    std::ofstream(folder.path() / "src" / "ui" / "Widget.cpp") << "x";
    std::ofstream(folder.path() / ".env") << "x";
    std::ofstream(folder.path() / ".git" / "objects" / "pack") << "x";
    std::filesystem::create_directory_symlink(folder.path() / "src", folder.path() / "linked");
    auto walked = files::TreeWalk::walk(folder.path(), 10, {".git"}, running);
    ASSERT_TRUE(walked.hasValue());
    auto paths = walked.value().paths;
    std::ranges::sort(paths);

    EXPECT_EQ(paths, (std::vector<std::string>{".env", "src/ui/Widget.cpp"}));
    EXPECT_TRUE(walked.value().complete);
    EXPECT_FALSE(files::TreeWalk::walk(folder.path(), 1, {".git"}, running).value().complete);

    // A folder that cannot be opened is left out and the walk goes on through the folders beside it.
    std::filesystem::create_directories(folder.path() / "locked");
    std::filesystem::create_directories(folder.path() / "zeta");
    std::ofstream(folder.path() / "locked" / "hidden.txt") << "x";
    std::ofstream(folder.path() / "zeta" / "last.txt") << "x";
    std::filesystem::permissions(folder.path() / "locked", std::filesystem::perms::none);
    auto around = files::TreeWalk::walk(folder.path(), 10, {".git"}, running);
    std::filesystem::permissions(folder.path() / "locked", std::filesystem::perms::owner_all);
    ASSERT_TRUE(around.hasValue());

    EXPECT_NE(std::ranges::find(around.value().paths, "zeta/last.txt"), around.value().paths.end());
    EXPECT_NE(std::ranges::find(around.value().paths, "src/ui/Widget.cpp"), around.value().paths.end());
    // A walk whose stop was requested ends at once and says it is incomplete.
    const std::atomic<bool> stopping{true};
    auto stopped = files::TreeWalk::walk(folder.path(), 10, {".git"}, stopping);
    ASSERT_TRUE(stopped.hasValue());
    EXPECT_TRUE(stopped.value().paths.empty());
    EXPECT_FALSE(stopped.value().complete);
}

// A search finds lines without regard to case in UTF-8 text files, skipping binary files, other encodings and files beyond the bound, and trims what it answers.
TEST(TextSearch, FindsLinesInTextFilesOnly) {
    tests::TemporaryDirectory folder;
    const std::atomic<bool> running{false};
    std::ofstream(folder.path() / "notes.md") << "first line\n    Call the Workpane API\r\nlast";
    std::ofstream(folder.path() / "binary.bin") << std::string("workpane\0data", 13);
    std::ofstream(folder.path() / "latin.txt") << "workpane \xE9";
    std::ofstream(folder.path() / "large.txt") << std::string(2000, 'x') << "workpane";
    auto found = files::TextSearch::search(folder.path(), "WORKPANE", 10, 1024, {}, running);
    ASSERT_TRUE(found.hasValue());

    ASSERT_EQ(found.value().matches.size(), 1U);
    EXPECT_EQ(found.value().matches[0].path, "notes.md");
    EXPECT_EQ(found.value().matches[0].line, 2U);
    EXPECT_EQ(found.value().matches[0].text, "Call the Workpane API");
    EXPECT_TRUE(found.value().complete);

    std::ofstream(folder.path() / "more.md") << "workpane\nworkpane\n";
    EXPECT_FALSE(files::TextSearch::search(folder.path(), "workpane", 2, 1024, {}, running).value().complete);

    // A line holding the text twice is answered once, the lines after it keep their numbers, and a text that spans lines is held by none.
    tests::TemporaryDirectory counted;
    std::ofstream(counted.path() / "lines.txt") << "a\nWORKPANE one workpane two\nb\nc\nworkpane\n";
    const auto lines = files::TextSearch::search(counted.path(), "workpane", 10, 1024, {}, running);
    ASSERT_TRUE(lines.hasValue());
    ASSERT_EQ(lines.value().matches.size(), 2U);
    EXPECT_EQ(lines.value().matches[0].line, 2U);
    EXPECT_EQ(lines.value().matches[0].text, "WORKPANE one workpane two");
    EXPECT_EQ(lines.value().matches[1].line, 5U);
    EXPECT_TRUE(files::TextSearch::search(counted.path(), "b\nc", 10, 1024, {}, running).value().matches.empty());
}

// Base64 gives back the bytes of every length of text, and a text with a foreign character, a wrong length or padding inside is refused whole.
TEST(Base64, DecodesTheStandardAlphabet) {
    // clang-format off
    const auto bytes = [](std::string_view text) { return std::vector<unsigned char>(text.begin(), text.end()); };
    // clang-format on
    EXPECT_EQ(text::Base64::decode(""), std::vector<unsigned char>{});
    EXPECT_EQ(text::Base64::decode("TQ=="), bytes("M"));
    EXPECT_EQ(text::Base64::decode("TWE="), bytes("Ma"));
    EXPECT_EQ(text::Base64::decode("TWFu"), bytes("Man"));
    EXPECT_EQ(text::Base64::decode("+/+/"), (std::vector<unsigned char>{0xFB, 0xFF, 0xBF}));
    EXPECT_FALSE(text::Base64::decode("TWF").has_value());
    EXPECT_FALSE(text::Base64::decode("TW=u").has_value());
    EXPECT_FALSE(text::Base64::decode("TWF*").has_value());
    EXPECT_FALSE(text::Base64::decode("T===").has_value());
}

// A text is UTF-8 only when every sequence is complete and shortest, and a cut never splits a character.
TEST(Utf8, ValidatesAndCutsAtCharacterBoundaries) {
    EXPECT_TRUE(text::Utf8::valid("a\xC3\xA7\xE2\x82\xAC\xF0\x9F\x98\x80"));
    EXPECT_FALSE(text::Utf8::valid("\xC0\xAF"));
    EXPECT_FALSE(text::Utf8::valid("\xED\xA0\x80"));
    EXPECT_FALSE(text::Utf8::valid("\xE2\x82"));
    EXPECT_EQ(text::Utf8::truncated("a\xC3\xA7\xE2\x82\xAC", 2), "a\xC3\xA7");
    EXPECT_EQ(text::Utf8::truncated("abc", 10), "abc");
}

} // namespace workpane