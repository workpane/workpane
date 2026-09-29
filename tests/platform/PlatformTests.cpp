#include "Error.h"
#include "audio/MiniaudioOutput.h"
#include "platform/DownloadTarget.h"
#include "platform/FileAccess.h"
#include "platform/FileReplacement.h"
#include "platform/NativeDialogService.h"
#include "platform/NativeProcesses.h"
#include "platform/NativePseudoTerminals.h"
#include "platform/NativeSystemInspector.h"
#include "platform/NativeSystemServices.h"
#include "platform/PathText.h"
#include "platform/PlatformWindow.h"
#include "platform/ProcessSignals.h"
#include "platform/ScrollWheel.h"
#include "platform/ShellCommand.h"
#include "platform/TerminalEnvironment.h"
#include "platform/posix/ShellHistory.h"
#include "platform/windows/WindowsEnvironment.h"
#include "process/ProcessEnd.h"
#include "process/ProcessEvents.h"
#include "process/ProcessLaunch.h"
#include "process/ProcessStream.h"
#include "support/TemporaryDirectory.h"
#include "support/WaveFile.h"
#include "support/WebViewProbe.h"
#include "ui/Fonts.h"
#include "ui/PseudoTerminal.h"
#include "ui/TerminalLaunch.h"
#include "ui/WebNavigation.h"
#include "ui/WebPermission.h"

#if defined(__linux__)
#include "platform/linux/LinuxEventWatcher.h"

#include <glib.h>
#endif

#include <gtest/gtest.h>
#include <imgui.h>
#include <nlohmann/json.hpp>

#if !defined(_WIN32)
#include <pthread.h>
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace workpane::platform {

#if defined(_WIN32)
// A path reaches Lua with forward slashes on Windows as everywhere else, a drive keeps its root, a network share reads as two slashes and its server, and a lone surrogate in a name becomes one replacement instead of ending the product.
TEST(PathText, WritesWindowsPathsWithForwardSlashes) {
    EXPECT_EQ(PathText::generic(std::filesystem::path(L"C:\\Users\\reader\\notes.txt")), "C:/Users/reader/notes.txt");
    EXPECT_EQ(PathText::generic(std::filesystem::path(L"C:\\")), "C:/");
    EXPECT_EQ(PathText::generic(std::filesystem::path(L"\\\\server\\share\\docs")), "//server/share/docs");
    EXPECT_EQ(PathText::utf8(std::filesystem::path(std::wstring{L'a', static_cast<wchar_t>(0xD800), L'b'})), "a\xEF\xBF\xBD"
                                                                                                             "b");
}
#endif

// A path is written as UTF-8, whatever characters its name holds.
TEST(PathText, WritesAPathAsUtf8) {
    const std::u8string named = u8"/home/reader/caf\u00e9/notes.txt";

    EXPECT_EQ(PathText::generic(std::filesystem::path(named)), std::string(named.begin(), named.end()));
    EXPECT_EQ(PathText::utf8(std::filesystem::path(named).filename()), "notes.txt");
}

// A real sound decodes and plays through miniaudio without a device, ends and is forgotten, a looping one plays until it is stopped, a file that cannot be opened ends with its reason, and a sound stopped before it was opened never starts.
TEST(MiniaudioOutput, PlaysAFileUntilItEndsOrIsStopped) {
    tests::TemporaryDirectory folder;
    tests::WaveFile::write(folder.path() / "tone.wav", 0.1);
    audio::MiniaudioOutput output(true);
    const std::uint64_t played = output.play(folder.path() / "tone.wav", 0.5F, false);
    const std::uint64_t looping = output.play(folder.path() / "tone.wav", 1.0F, true);
    bool ended = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    while (!ended && std::chrono::steady_clock::now() < deadline) {
        const auto finished = output.update();
        ended = std::ranges::find(finished, played, &audio::AudioOutput::Ended::sound) != finished.end();
        EXPECT_EQ(std::ranges::find(finished, looping, &audio::AudioOutput::Ended::sound), finished.end());
        // clang-format off
        EXPECT_TRUE(std::ranges::none_of(finished, [](const audio::AudioOutput::Ended& sound) { return sound.failure.has_value(); }));
        // clang-format on
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ASSERT_TRUE(ended);
    output.stop(looping);
    EXPECT_TRUE(output.update().empty());

    // A file that cannot be opened ends at a later update with the reason instead of failing the call.
    const std::uint64_t missing = output.play(folder.path() / "missing.wav", 1.0F, false);
    std::vector<audio::AudioOutput::Ended> unplayable;
    const auto opening = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    while (unplayable.empty() && std::chrono::steady_clock::now() < opening) {
        unplayable = output.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ASSERT_EQ(unplayable.size(), 1U);
    EXPECT_EQ(unplayable[0].sound, missing);
    ASSERT_TRUE(unplayable[0].failure.has_value());
    EXPECT_EQ(unplayable[0].failure->code, "audio_file_unreadable");
    EXPECT_TRUE(output.update().empty());

    // A sound stopped before it was opened never starts, and the loop hears each sound the worker opened.
    std::atomic<int> wakes{0};
    // clang-format off
    output.setWakeHandler([&wakes]() { ++wakes; });
    // clang-format on
    const std::uint64_t cancelled = output.play(folder.path() / "tone.wav", 1.0F, true);
    output.stop(cancelled);
    const std::uint64_t heard = output.play(folder.path() / "tone.wav", 1.0F, false);
    bool finished = false;
    const auto hearing = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    while (!finished && std::chrono::steady_clock::now() < hearing) {
        const auto over = output.update();
        finished = std::ranges::find(over, heard, &audio::AudioOutput::Ended::sound) != over.end();
        EXPECT_EQ(std::ranges::find(over, cancelled, &audio::AudioOutput::Ended::sound), over.end());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(finished);
    EXPECT_GE(wakes.load(), 1);
    output.setWakeHandler(nullptr);
}

TEST(NativeSystemInspector, DescribesTheMachineItRunsOn) {
    NativeSystemInspector inspector;
    const auto snapshot = inspector.inspect();
    ASSERT_TRUE(snapshot.hasValue());
    const nlohmann::json& machine = snapshot.value();

    // clang-format off
    const auto printable = [](const std::string& text) {
        for (const char character : text) {
            if (std::iscntrl(static_cast<unsigned char>(character)) != 0) {
                return false;
            }
        }

        return true;
    };
    // clang-format on

    EXPECT_FALSE(machine["os"]["hostName"].get<std::string>().empty());
    EXPECT_FALSE(machine["os"]["name"].get<std::string>().empty());
    EXPECT_TRUE(printable(machine["os"]["version"].get<std::string>()));
    EXPECT_EQ(machine["os"]["architectureBits"].get<int>(), 64);
    ASSERT_FALSE(machine["processors"].empty());
    EXPECT_GE(machine["processors"][0]["logicalCores"].get<std::int64_t>(), machine["processors"][0]["physicalCores"].get<std::int64_t>());
    EXPECT_GT(machine["processors"][0]["physicalCores"].get<std::int64_t>(), 0);
    EXPECT_GT(machine["memory"]["total"].get<std::int64_t>(), 0);
    EXPECT_LE(machine["memory"]["available"].get<std::int64_t>(), machine["memory"]["total"].get<std::int64_t>());

    if (machine["processorUsage"].contains("utilization")) {
        EXPECT_GE(machine["processorUsage"]["utilization"].get<double>(), 0.0);
        EXPECT_LE(machine["processorUsage"]["utilization"].get<double>(), 1.0);
    }

    for (const auto& disk : machine["disks"]) {
        EXPECT_GT(disk["size"].get<std::int64_t>(), 0);
    }

    for (const auto& battery : machine["batteries"]) {
        if (battery.contains("capacity")) {
            EXPECT_GE(battery["capacity"].get<double>(), 0.0);
            EXPECT_LE(battery["capacity"].get<double>(), 1.0);
        }
    }
}

// The system names its zone as the time zone database does, and a zone answers the offset it has at an instant, summer time included.
TEST(NativeSystemServices, AnswersTheOffsetsOfTimeZones) {
    NativeSystemServices system;
    const std::string zone = system.timeZone().value();
    EXPECT_FALSE(zone.empty());
    EXPECT_TRUE(system.zoneOffset(zone, 0).hasValue());

    EXPECT_EQ(system.zoneOffset("UTC", 1774746000).value(), 0);
    EXPECT_EQ(system.zoneOffset("Europe/Lisbon", 1774745999).value(), 0);
    EXPECT_EQ(system.zoneOffset("Europe/Lisbon", 1774746000).value(), 3600);
    EXPECT_EQ(system.zoneOffset("America/Sao_Paulo", 1774746000).value(), -10800);
    EXPECT_EQ(system.zoneOffset("Nowhere/Missing", 0).error().code, "time_zone_unknown");
}

#if defined(__linux__)
// Opening an address answers once the opener handed it on, even while the opener stays with the browser it started, and an opener reporting a failure is answered with it.
// The opener receives its three streams alone, so a browser it starts holds no descriptor of the product.
TEST(NativeSystemServices, OpensAnAddressWithoutWaitingForTheBrowser) {
    tests::TemporaryDirectory directory;
    std::array<int, 2> leaked{};
    ASSERT_EQ(::pipe(leaked.data()), 0);
    const std::filesystem::path opener = directory.path() / "xdg-open";
    std::ofstream(opener) << "#!/bin/sh\nfor fd in " << leaked[0] << " " << leaked[1] << "; do [ -e /dev/fd/$fd ] && exit 5; done\ncase \"$1\" in *fails*) exit 4;; esac\nexec sleep 5 </dev/null >/dev/null 2>&1\n";
    std::filesystem::permissions(opener, std::filesystem::perms::owner_all);
    const std::string path = std::getenv("PATH");
    ASSERT_EQ(::setenv("PATH", (directory.path().string() + ":" + path).c_str(), 1), 0);
    NativeSystemServices services;
    const auto begun = std::chrono::steady_clock::now();
    const auto opened = services.openUrl("https://example.com/stays");
    const auto waited = std::chrono::steady_clock::now() - begun;
    const auto refused = services.openUrl("https://example.com/fails");
    ::setenv("PATH", path.c_str(), 1);
    ::close(leaked[0]);
    ::close(leaked[1]);

    EXPECT_TRUE(opened.hasValue());
    EXPECT_LT(waited, std::chrono::seconds(4));
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.error().code, "system_url_failed");
}
#endif

#if defined(__linux__)
// A native dialog still open when the product quits is closed with it, so quitting never waits for the reader to answer a dialog of a product already gone.
TEST(NativeDialogService, ClosesADialogStillOpenWhenItGoesAway) {
    tests::TemporaryDirectory directory;
    const std::filesystem::path helper = directory.path() / "zenity";
    std::ofstream(helper) << "#!/bin/sh\ncase \"$1\" in --version) echo 3.44.0; exit 0;; esac\nexec sleep 30 </dev/null >/dev/null 2>&1\n";
    std::filesystem::permissions(helper, std::filesystem::perms::owner_all);
    const std::string path = std::getenv("PATH");
    ASSERT_EQ(::setenv("PATH", (directory.path().string() + ":" + path).c_str(), 1), 0);
    bool answered = false;
    const auto begun = std::chrono::steady_clock::now();

    {
        NativeDialogService dialogs;
        // clang-format off
        dialogs.openFiles("Open", directory.path().string(), {}, false, [&answered](Result<std::vector<std::string>>) { answered = true; });
        // clang-format on
        dialogs.poll();
        EXPECT_TRUE(dialogs.pending());
    }

    ::setenv("PATH", path.c_str(), 1);

    EXPECT_LT(std::chrono::steady_clock::now() - begun, std::chrono::seconds(10));
    EXPECT_FALSE(answered);
}
#endif

#if defined(_WIN32)
// A new process receives its environment sorted without regard to case, as Windows keeps it, whatever order it was built in.
TEST(WindowsEnvironment, SortsTheBlockWithoutRegardToCase) {
    std::wstring expected;

    for (const std::wstring entry : {L"=C:=C:\\work", L"A=1", L"b=2", L"c=3"}) {
        expected += entry;
        expected.push_back(L'\0');
    }

    expected.push_back(L'\0');

    EXPECT_EQ(WindowsEnvironment::block({"c=3", "b=2", "=C:=C:\\work", "A=1"}), expected);
}
#endif

// A dropped path reaches the shell quoted the way that shell reads it literally.
TEST(NativePseudoTerminals, QuotesDroppedPathsForTheShellThatReadsThem) {
    const NativePseudoTerminals host;
#if defined(_WIN32)
    EXPECT_EQ(host.quotePaths({L"C:\\it's $HOME\\a.txt"}, "C:/Program Files/Git/bin/bash.exe"), "'C:\\it'\\''s $HOME\\a.txt' ");
    EXPECT_EQ(host.quotePaths({L"C:\\it's\\a.txt"}, "C:/Program Files/PowerShell/7/pwsh.exe"), "'C:\\it''s\\a.txt' ");
    EXPECT_EQ(host.quotePaths({L"C:\\a b\\c.txt"}, "C:/Windows/System32/cmd.exe"), "\"C:\\a b\\c.txt\" ");
#else
    EXPECT_EQ(host.quotePaths({"/tmp/it's $HOME/a.txt"}, "/bin/sh"), "'/tmp/it'\\''s $HOME/a.txt' ");
#endif
}

// The shell is looked up on the search path alone, so a program named like PowerShell in the folder the product started from never runs as the shell.
#if defined(_WIN32)
TEST(NativePseudoTerminals, NeverTakesTheShellFromTheCurrentFolder) {
    tests::TemporaryDirectory folder;
    std::ofstream(folder.path() / "pwsh.exe", std::ios::binary) << "planted";
    const std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::current_path(folder.path());
    const NativePseudoTerminals host;
    const std::filesystem::path chosen = host.shellProgram();
    std::filesystem::current_path(previous);

    EXPECT_NE(std::filesystem::weakly_canonical(chosen), std::filesystem::weakly_canonical(folder.path() / "pwsh.exe"));
    EXPECT_TRUE(chosen.is_absolute());
}
#endif

// The product knows its own process, which a language server it starts watches so it ends with the product.
TEST(NativeSystemServices, AnswersItsOwnProcess) {
    NativeSystemServices system;
    EXPECT_GT(system.processId(), 0);
}

// The machine offers monospaced families, each named and read from a file that exists.
TEST(NativeSystemServices, OffersTheMonospacedFamiliesOfTheMachine) {
    NativeSystemServices system;
    const auto fonts = system.monospaceFonts();
    ASSERT_FALSE(fonts.empty());

    for (const auto& font : fonts) {
        EXPECT_FALSE(font.family.empty());
        EXPECT_TRUE(std::filesystem::is_regular_file(font.file)) << font.file;
    }
}

// Every monospaced family the machine offers can be read into the atlas from its file, whether the file holds one face or several.
TEST(NativeSystemServices, OffersFamiliesTheFontsCanRead) {
    NativeSystemServices system;
    ImGui::CreateContext();
    ui::Fonts fonts;

    for (const auto& font : system.monospaceFonts()) {
        std::ifstream stream(font.file, std::ios::binary);
        std::vector<unsigned char> data((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        const auto adopted = fonts.adopt(*ImGui::GetIO().Fonts, font.family, std::move(data));
        EXPECT_TRUE(adopted.hasValue()) << font.family << " " << font.file;
        EXPECT_EQ(fonts.has(font.family), adopted.hasValue());
    }

    ImGui::DestroyContext();
}

// A folder and a file the account created may be read and written, and a path that does not exist may be neither.
TEST(FileAccess, AnswersWhatTheAccountMayDo) {
    const tests::TemporaryDirectory folder;
    const std::filesystem::path file = folder.path() / "notes.txt";
    std::ofstream(file) << "notes";

    EXPECT_TRUE(FileAccess::readable(folder.path()));
    EXPECT_TRUE(FileAccess::writable(folder.path()));
    EXPECT_TRUE(FileAccess::readable(file));
    EXPECT_TRUE(FileAccess::writable(file));
    EXPECT_FALSE(FileAccess::readable(folder.path() / "missing"));
    EXPECT_FALSE(FileAccess::writable(folder.path() / "missing"));
}

// A download keeps the plain name its page suggests inside the folder, which is created when missing, gains a counter before its extension while a file holds that name, and a name that would leave the folder or says nothing is replaced.
TEST(DownloadTarget, SavesUnderAPlainNameNoFileHolds) {
    const tests::TemporaryDirectory folder;
    const std::filesystem::path downloads = folder.path() / "Downloads";

    EXPECT_EQ(DownloadTarget::choose(downloads, "report.pdf"), downloads / "report.pdf");
    EXPECT_TRUE(std::filesystem::is_directory(downloads));
    std::ofstream(downloads / "report.pdf") << "first";
    EXPECT_EQ(DownloadTarget::choose(downloads, "report.pdf"), downloads / "report (1).pdf");
    std::ofstream(downloads / "report (1).pdf") << "second";
    EXPECT_EQ(DownloadTarget::choose(downloads, "report.pdf"), downloads / "report (2).pdf");
    EXPECT_EQ(DownloadTarget::choose(downloads, "../../etc/passwd"), downloads / "passwd");
    EXPECT_EQ(DownloadTarget::choose(downloads, "..\\notes\\secret.txt"), downloads / "secret.txt");
    EXPECT_EQ(DownloadTarget::choose(downloads, ".."), downloads / "download");
    EXPECT_EQ(DownloadTarget::choose(downloads, ""), downloads / "download");
}

// A file moves over another one in one step even while another program holds the destination for a moment, which Windows refuses until it lets go, and a folder never takes the place of a file.
TEST(FileReplacement, MovesAFileOverAnotherHeldForAMoment) {
    const tests::TemporaryDirectory folder;
    const std::filesystem::path source = folder.path() / "next.txt";
    const std::filesystem::path destination = folder.path() / "notes.txt";
    std::ofstream(source, std::ios::binary) << "new";
    std::ofstream(destination, std::ios::binary) << "old";
    std::atomic<bool> held{false};
    // clang-format off
    std::thread holder([&destination, &held]() {
        const std::ifstream file(destination, std::ios::binary);
        held = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    });
    // clang-format on

    while (!held) {
        std::this_thread::yield();
    }

    EXPECT_FALSE(FileReplacement::replace(source, destination));
    holder.join();
    std::ifstream moved(destination, std::ios::binary);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(moved), std::istreambuf_iterator<char>()), "new");
    EXPECT_FALSE(std::filesystem::exists(source));

    std::filesystem::create_directories(folder.path() / "folder");
    std::ofstream(source, std::ios::binary) << "again";
    EXPECT_TRUE(FileReplacement::replace(source, folder.path() / "folder"));
    EXPECT_TRUE(std::filesystem::exists(source));
}

// The shell of the reader runs behind a real pseudo-terminal, answers what it is sent, and its exit code arrives after everything it printed.
TEST(NativePseudoTerminals, RunsTheShellOfTheReaderUntilItExits) {
    tests::TemporaryDirectory directory;
    NativePseudoTerminals host;
    std::atomic<int> arrivals{0};
    // clang-format off
    host.listen([&arrivals]() { ++arrivals; });
    // clang-format on
    EXPECT_TRUE(host.shellProgram().is_absolute());
    EXPECT_EQ(host.start(ui::TerminalLaunch{directory.path() / "missing", 80, 24, {}, {}}).error().code, "terminal_directory_missing");

    auto started = host.start(ui::TerminalLaunch{directory.path(), 100, 30, {}, {}});
    ASSERT_TRUE(started.hasValue()) << started.error().code << " " << started.error().detail;
    auto& terminal = *started.value();
    ASSERT_TRUE(terminal.write("echo workpane-ready\r"));
    terminal.resize(120, 40);

    std::string output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);

    while (output.find("workpane-ready\r\n") == std::string::npos && std::chrono::steady_clock::now() < deadline) {
        output += terminal.takeOutput(4096);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_NE(output.find("workpane-ready\r\n"), std::string::npos) << output;

    // Windows tells the directory of a shell only through the marker its prompt writes, which the terminal component reads.
    // The echo of what was typed can arrive before the shell even started, so the directory is read once the shell itself computed an answer.
#if !defined(_WIN32)
    ASSERT_TRUE(terminal.write("echo shell-$((6*7))\r"));

    while (output.find("shell-42") == std::string::npos && std::chrono::steady_clock::now() < deadline) {
        output += terminal.takeOutput(4096);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ASSERT_NE(output.find("shell-42"), std::string::npos) << output;
    EXPECT_EQ(std::filesystem::path(terminal.directory()), std::filesystem::canonical(directory.path()));
#endif

    // A flood left unread pauses the reading of the shell, and every byte still arrives once it is taken slice by slice.
#if !defined(_WIN32)
    ASSERT_TRUE(terminal.write("head -c 1500000 /dev/zero | tr '\\0' x; echo; echo flood-$((6*7))\r"));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::size_t flooded = 0;
    std::string tail;
    bool done = false;
    const auto flooding = std::chrono::steady_clock::now() + std::chrono::seconds(60);

    // The marker is computed by the shell so the echo of the command never matches it, and it is looked for before the kept tail is cut, because the prompt after it may be longer than the tail.
    while (!done && std::chrono::steady_clock::now() < flooding) {
        const std::string slice = terminal.takeOutput(65536);
        EXPECT_LE(slice.size(), 65536U);
        flooded += static_cast<std::size_t>(std::ranges::count(slice, 'x'));
        const std::string joined = tail + slice;
        done = joined.find("flood-42\r\n") != std::string::npos;
        tail = joined.substr(joined.size() > 64 ? joined.size() - 64 : 0);
    }

    EXPECT_TRUE(done);
    EXPECT_GE(flooded, 1500000U);
#endif

    ASSERT_TRUE(terminal.write("exit 3\r"));
    const auto ending = std::chrono::steady_clock::now() + std::chrono::seconds(20);

    while (!terminal.exitCode().has_value() && std::chrono::steady_clock::now() < ending) {
        output += terminal.takeOutput(4096);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(terminal.exitCode(), 3);
    EXPECT_GT(arrivals.load(), 0);
    EXPECT_FALSE(terminal.write("echo late\r"));
}

#if defined(__linux__)
// A watch wakes the loop once a timeout of GLib is due and the source runs when the loop dispatches, while a watch the loop ends itself wakes nothing.
TEST(LinuxEventWatcher, WakesTheLoopOnlyWhenGtkHasSomethingToRun) {
    std::mutex mutex;
    std::condition_variable woken;
    int wakes = 0;
    bool ran = false;
    // clang-format off
    LinuxEventWatcher watcher([&mutex, &woken, &wakes]() {
        {
            const std::lock_guard lock(mutex);
            ++wakes;
        }

        woken.notify_all();
    });
    // clang-format on

    watcher.watch();
    watcher.dispatch();
    EXPECT_EQ(wakes, 0);

    // clang-format off
    g_timeout_add(50, [](gpointer flag) -> gboolean { *static_cast<bool*>(flag) = true; return G_SOURCE_REMOVE; }, &ran);
    // clang-format on
    watcher.watch();
    std::unique_lock lock(mutex);
    // clang-format off
    ASSERT_TRUE(woken.wait_for(lock, std::chrono::seconds(5), [&wakes]() { return wakes > 0; }));
    // clang-format on
    lock.unlock();
    EXPECT_FALSE(ran);

    watcher.dispatch();
    EXPECT_TRUE(ran);
    EXPECT_EQ(wakes, 1);
}
#endif

// Waiting without a deadline sleeps until a wake arrives, so an idle product never spins, which the largest finite timeout did on Linux because GLFW turns it into a poll interval the system refuses.
TEST(PlatformWindow, SleepsUntilAWakeWithoutADeadline) {
#if defined(__linux__)
    if (std::getenv("DISPLAY") == nullptr) {
        GTEST_SKIP() << "No X display runs";
    }
#endif

    // clang-format off
    ASSERT_TRUE(PlatformWindow::initialize([](int, const std::string&) {}).hasValue());
    // clang-format on
    auto window = PlatformWindow::create("Workpane", std::nullopt);

    // A virtual machine without a graphics device, such as a runner of continuous integration on macOS or Windows, offers no OpenGL 3.2 to open the window with.
    if (!window.hasValue() && window.error().code == "window_opengl_unavailable") {
        PlatformWindow::terminate();
        GTEST_SKIP() << window.error().detail;
    }

    ASSERT_TRUE(window.hasValue()) << window.error().code << ": " << window.error().detail;
    window.value()->pollEvents();
    std::atomic<bool> woken{false};
    // clang-format off
    const auto wake = [&woken]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        woken = true;
        PlatformWindow::wake();
    };
    // clang-format on

    std::thread waker(wake);
    int returns = 0;

    while (!woken) {
        window.value()->waitEvents();
        ++returns;
    }

    waker.join();
    EXPECT_LT(returns, 50);
    window.value().reset();
    PlatformWindow::terminate();
}

// A step of the wheel scrolls ten points on macOS, where the window counts a trackpad in tenths of what the fingers travel, and three lines of text elsewhere, where it counts notches.
TEST(ScrollWheel, ScrollsAsFarAsTheSystemForEachStep) {
#if defined(__APPLE__)
    EXPECT_FLOAT_EQ(platform::ScrollWheel::points(16.0F), 10.0F);
    EXPECT_FLOAT_EQ(platform::ScrollWheel::points(20.0F), 10.0F);
#else
    EXPECT_FLOAT_EQ(platform::ScrollWheel::points(16.0F), 48.0F);
    EXPECT_FLOAT_EQ(platform::ScrollWheel::points(20.0F), 60.0F);
#endif
}

// A shell started from a terminal of Workpane receives the folder and the history of the reader rather than what that terminal exported for its own shell, and every shell starts in UTF-8 with the variables of this terminal.
TEST(TerminalEnvironment, LeavesOutWhatAnotherTerminalExportedForItsShell) {
    const std::vector<std::string> nested{"HOME=/home/reader", "PATH=/bin", "TERM=screen", "HISTFILE=/parent/one.history", "ZDOTDIR=/parent/zsh", "WORKPANE_ZDOTDIR=/parent/zsh", "WORKPANE_USER_ZDOTDIR=/home/reader/.config/zsh", "WORKPANE_HISTORY_FILE=/parent/one.history", "LANG=pt_BR.UTF-8"};
    EXPECT_EQ(TerminalEnvironment::build(nested, {"HISTFILE=/data/two.history"}), (std::vector<std::string>{"HOME=/home/reader", "PATH=/bin", "LANG=pt_BR.UTF-8", "TERM=xterm-256color", "COLORTERM=truecolor", "TERM_PROGRAM=Workpane", "ZDOTDIR=/home/reader/.config/zsh", "HISTFILE=/data/two.history"}));
    EXPECT_EQ(TerminalEnvironment::zshFolder(nested), "/home/reader/.config/zsh");

    const std::vector<std::string> recordedHome{"HOME=/home/reader", "ZDOTDIR=/parent/zsh", "WORKPANE_ZDOTDIR=/parent/zsh", "WORKPANE_USER_ZDOTDIR=/home/reader"};
    EXPECT_EQ(TerminalEnvironment::build(recordedHome, {}), (std::vector<std::string>{"HOME=/home/reader", "TERM=xterm-256color", "COLORTERM=truecolor", "TERM_PROGRAM=Workpane", "LANG=en_US.UTF-8"}));
    EXPECT_EQ(TerminalEnvironment::zshFolder(recordedHome), "/home/reader");

    const std::vector<std::string> chosen{"HOME=/home/reader", "ZDOTDIR=/home/reader/.zsh"};
    EXPECT_EQ(TerminalEnvironment::build(chosen, {"ZDOTDIR=/data/zsh"}), (std::vector<std::string>{"HOME=/home/reader", "TERM=xterm-256color", "COLORTERM=truecolor", "TERM_PROGRAM=Workpane", "LANG=en_US.UTF-8", "ZDOTDIR=/data/zsh"}));
    EXPECT_EQ(TerminalEnvironment::zshFolder(chosen), "/home/reader/.zsh");
    EXPECT_EQ(TerminalEnvironment::zshFolder({"HOME=/home/reader"}), "/home/reader");
}

#if !defined(_WIN32)
// A write to a pipe whose reader went away fails with a broken pipe instead of ending the process, even on a thread that does not block the signal.
TEST(ProcessSignals, TurnsAWriteNobodyReadsIntoAFailure) {
    sigset_t broken;
    sigemptyset(&broken);
    sigaddset(&broken, SIGPIPE);
    ASSERT_EQ(::pthread_sigmask(SIG_UNBLOCK, &broken, nullptr), 0);
    platform::ProcessSignals::ignoreBrokenPipes();
    std::array<int, 2> ends{};
    ASSERT_EQ(::pipe(ends.data()), 0);
    ::close(ends[0]);
    errno = 0;
    const auto written = ::write(ends[1], "x", 1);
    const int failure = errno;
    ::close(ends[1]);

    EXPECT_EQ(written, -1);
    EXPECT_EQ(failure, EPIPE);
}

// A terminal given a shell and a history file of its own starts that shell with its history there.
TEST(NativePseudoTerminals, StartsTheShellItNamesWithAHistoryOfItsOwn) {
    tests::TemporaryDirectory directory;
    NativePseudoTerminals host;
    // clang-format off
    host.listen([]() {});
    // clang-format on
    const std::filesystem::path history = directory.path() / "history" / "one.history";
    // clang-format off
    const auto answered = [](ui::PseudoTerminal& terminal, std::string_view marker) {
        std::string output;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);

        while (output.find(marker) == std::string::npos && std::chrono::steady_clock::now() < deadline) {
            output += terminal.takeOutput(4096);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        return output;
    };
    // clang-format on

    auto plain = host.start(ui::TerminalLaunch{directory.path(), 100, 30, "/bin/sh", history});
    ASSERT_TRUE(plain.hasValue()) << plain.error().code;
    ASSERT_TRUE(plain.value()->write("echo HIST=$HISTFILE:END\r"));
    EXPECT_NE(answered(*plain.value(), "HIST=" + history.string() + ":END").find("HIST=" + history.string() + ":END"), std::string::npos);
    EXPECT_EQ(host.start(ui::TerminalLaunch{directory.path(), 100, 30, directory.path() / "missing-shell", {}}).error().code, "terminal_shell_not_executable");
}

// The startup files of zsh are written once and replaced whole only when they differ, so a zsh reading them while another terminal starts never reads a file half written.
TEST(ShellHistory, WritesTheStartupFilesOfZshOnlyWhenTheyDiffer) {
    tests::TemporaryDirectory directory;
    const ShellCommand zsh{"/bin/zsh", {"-l"}, "zsh"};
    const std::filesystem::path history = directory.path() / "history" / "one.history";
    ASSERT_TRUE(ShellHistory::environment(zsh, history, {}).hasValue());
    const std::filesystem::path startup = directory.path() / "history" / "zsh" / ".zshrc";
    std::filesystem::last_write_time(startup, std::filesystem::last_write_time(startup) - std::chrono::hours(1));
    const auto aged = std::filesystem::last_write_time(startup);

    ASSERT_TRUE(ShellHistory::environment(zsh, directory.path() / "history" / "two.history", {}).hasValue());
    EXPECT_EQ(std::filesystem::last_write_time(startup), aged);

    std::ofstream(startup, std::ios::binary | std::ios::trunc) << "stale";
    ASSERT_TRUE(ShellHistory::environment(zsh, history, {}).hasValue());
    std::ifstream replaced(startup, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(replaced)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("fc -p"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(directory.path() / "history" / "zsh" / ".zshrc.staged"));

    // A startup file that cannot be staged whole leaves the one in place as it was.
    std::ofstream(startup, std::ios::binary | std::ios::trunc) << "kept";
    std::filesystem::create_directories(directory.path() / "history" / "zsh" / ".zshrc.staged" / "held");
    EXPECT_EQ(ShellHistory::environment(zsh, history, {}).error().code, "terminal_history_unavailable");
    std::ifstream kept(startup, std::ios::binary);
    EXPECT_EQ(std::string((std::istreambuf_iterator<char>(kept)), std::istreambuf_iterator<char>()), "kept");
}

// A shell that ends while a program it left in the background still holds the terminal reports its end at once, instead of when that program lets go of the terminal.
TEST(NativePseudoTerminals, ReportsTheEndOfAShellThatLeftAProgramRunning) {
    tests::TemporaryDirectory directory;
    NativePseudoTerminals host;
    // clang-format off
    host.listen([]() {});
    // clang-format on
    auto terminal = host.start(ui::TerminalLaunch{directory.path(), 100, 30, "/bin/sh", {}});
    ASSERT_TRUE(terminal.hasValue()) << terminal.error().code;
    ASSERT_TRUE(terminal.value()->write("sleep 30 & exit 7\r"));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    // The end is told once the output before it was read, as a terminal reads it.
    while (!terminal.value()->exitCode().has_value() && std::chrono::steady_clock::now() < deadline) {
        std::ignore = terminal.value()->takeOutput(65536);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    EXPECT_EQ(terminal.value()->exitCode(), 7);
    EXPECT_LT(std::chrono::steady_clock::now(), deadline);
}

// A shell that let go of its terminal while its program keeps running still ends with the hangup when the terminal closes, so the product never waits for that program.
TEST(NativePseudoTerminals, EndsAProgramThatLetGoOfItsTerminal) {
    tests::TemporaryDirectory directory;
    const auto begun = std::chrono::steady_clock::now();

    {
        NativePseudoTerminals host;
        // clang-format off
        host.listen([]() {});
        // clang-format on
        auto terminal = host.start(ui::TerminalLaunch{directory.path(), 100, 30, "/bin/sh", {}});
        ASSERT_TRUE(terminal.hasValue()) << terminal.error().code;
        ASSERT_TRUE(terminal.value()->write("exec sleep 30 </dev/null >/dev/null 2>&1\r"));
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        EXPECT_FALSE(terminal.value()->exitCode().has_value());
        terminal.value().reset();
    }

    EXPECT_LT(std::chrono::steady_clock::now() - begun, std::chrono::seconds(10));
}

// A shell that ended reports no directory, since the number of its process may already name another one.
TEST(NativePseudoTerminals, ReportsNoDirectoryOnceItsShellEnded) {
    tests::TemporaryDirectory directory;
    NativePseudoTerminals host;
    // clang-format off
    host.listen([]() {});
    // clang-format on
    auto terminal = host.start(ui::TerminalLaunch{directory.path(), 100, 30, "/bin/sh", {}});
    ASSERT_TRUE(terminal.hasValue()) << terminal.error().code;
    ASSERT_TRUE(terminal.value()->write("exit\r"));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);

    while (!terminal.value()->exitCode().has_value() && std::chrono::steady_clock::now() < deadline) {
        std::ignore = terminal.value()->takeOutput(4096);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ASSERT_TRUE(terminal.value()->exitCode().has_value());
    EXPECT_EQ(terminal.value()->directory(), "");
}

// A shell receives its terminal alone, even a descriptor another part of the process left open to programs, and its line discipline edits UTF-8.
TEST(NativePseudoTerminals, StartsAShellWithItsTerminalAloneEditingUtf8) {
    tests::TemporaryDirectory directory;
    std::array<int, 2> leaked{};
    ASSERT_EQ(::pipe(leaked.data()), 0);
    NativePseudoTerminals host;
    // clang-format off
    host.listen([]() {});
    // clang-format on
    auto terminal = host.start(ui::TerminalLaunch{directory.path(), 100, 30, "/bin/sh", {}});
    ASSERT_TRUE(terminal.hasValue()) << terminal.error().code;
    const std::string probe = "for fd in " + std::to_string(leaked[0]) + " " + std::to_string(leaked[1]) + "; do [ -e /dev/fd/$fd ] && echo OPEN-$fd; done; stty -a | tr ' ' '\\n' | grep -x iutf8 | tr a-z A-Z; echo PROBE-$((6*7))\r";
    ASSERT_TRUE(terminal.value()->write(probe));
    std::string output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);

    while (output.find("PROBE-42") == std::string::npos && std::chrono::steady_clock::now() < deadline) {
        output += terminal.value()->takeOutput(4096);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ::close(leaked[0]);
    ::close(leaked[1]);

    EXPECT_NE(output.find("PROBE-42"), std::string::npos) << output;
    EXPECT_EQ(output.find("OPEN-" + std::to_string(leaked[0])), std::string::npos) << output;
    EXPECT_EQ(output.find("OPEN-" + std::to_string(leaked[1])), std::string::npos) << output;
    EXPECT_NE(output.find("IUTF8"), std::string::npos) << output;
}

// Closing a terminal returns at once while its shell floods an output nobody reads, whether the shell writes as it hangs up or ignores the hangup.
// Each shell ends in the background, the one ignoring the hangup only after a grace, and the host waits for both before it is gone, with nothing reported once a terminal is closed.
TEST(NativePseudoTerminals, ClosesATerminalWithoutWaitingForItsShell) {
    tests::TemporaryDirectory directory;
    std::atomic<int> arrivals{0};
    int reported = 0;
    std::vector<pid_t> shells;

    {
        NativePseudoTerminals host;
        // clang-format off
        host.listen([&arrivals]() { ++arrivals; });
        // clang-format on

        for (const std::string_view trap : {"trap 'echo leaving; exit' HUP", "trap '' HUP"}) {
            const std::filesystem::path recorded = directory.path() / ("shell-" + std::to_string(shells.size()) + ".pid");
            auto terminal = host.start(ui::TerminalLaunch{directory.path(), 100, 30, "/bin/sh", {}});
            ASSERT_TRUE(terminal.hasValue()) << terminal.error().code;
            ASSERT_TRUE(terminal.value()->write(std::string(trap) + "; echo $$ > '" + recorded.string() + "'; while :; do echo flooding-the-terminal-nobody-reads; done\r"));
            pid_t shell = 0;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);

            while (shell == 0 && std::chrono::steady_clock::now() < deadline) {
                std::ifstream(recorded) >> shell;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            ASSERT_GT(shell, 0);
            std::this_thread::sleep_for(std::chrono::seconds(1));
            terminal.value().reset();
            reported = arrivals.load();
            shells.push_back(shell);
        }

        EXPECT_EQ(::kill(shells.back(), 0), 0);
    }

    for (const pid_t shell : shells) {
        EXPECT_EQ(::kill(shell, 0), -1);
        EXPECT_EQ(errno, ESRCH);
    }

    EXPECT_EQ(arrivals.load(), reported);
}

// Each terminal keeps the commands typed in it in its own history file and never in the file of another terminal, zsh as soon as a command runs and bash once its terminal closes.
TEST(NativePseudoTerminals, KeepsTheHistoryOfEachTerminalInItsOwnFile) {
    tests::TemporaryDirectory directory;
    const std::filesystem::path home = directory.path() / "home";
    std::filesystem::create_directories(home);
    ::setenv("HOME", home.c_str(), 1);
    ::unsetenv("ZDOTDIR");
    // clang-format off
    const auto read = [](const std::filesystem::path& file) { std::ifstream stream(file); return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()); };
    // clang-format on

    for (const std::filesystem::path shell : {"/bin/bash", "/bin/zsh"}) {
        if (!std::filesystem::exists(shell)) {
            continue;
        }

        const std::filesystem::path first = directory.path() / shell.filename() / "first.history";
        const std::filesystem::path second = directory.path() / shell.filename() / "second.history";

        {
            NativePseudoTerminals host;
            // clang-format off
            host.listen([]() {});
            // clang-format on
            auto one = host.start(ui::TerminalLaunch{directory.path(), 100, 30, shell, first});
            auto two = host.start(ui::TerminalLaunch{directory.path(), 100, 30, shell, second});
            ASSERT_TRUE(one.hasValue() && two.hasValue());
            ASSERT_TRUE(one.value()->write("echo first-$((20+22))\r"));
            ASSERT_TRUE(two.value()->write("echo second-$((20+22))\r"));
            std::string firstOutput;
            std::string secondOutput;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);

            while ((firstOutput.find("first-42") == std::string::npos || secondOutput.find("second-42") == std::string::npos) && std::chrono::steady_clock::now() < deadline) {
                firstOutput += one.value()->takeOutput(4096);
                secondOutput += two.value()->takeOutput(4096);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        EXPECT_NE(read(first).find("echo first-$((20+22))"), std::string::npos) << shell << read(first);
        EXPECT_EQ(read(first).find("second-"), std::string::npos) << shell;
        EXPECT_NE(read(second).find("echo second-$((20+22))"), std::string::npos) << shell << read(second);
        EXPECT_EQ(read(second).find("first-"), std::string::npos) << shell;
    }
}

// Every startup file the reader keeps runs before the first prompt, as a login shell reads them on macOS and an interactive one elsewhere, whether the terminal keeps a history of its own or not.
// A product started from a terminal of Workpane gives its shells the files and the history of the reader too, never the integration folder or the history of that terminal, so no startup file reads itself again.
TEST(NativePseudoTerminals, LoadsTheStartupFilesOfTheReader) {
    tests::TemporaryDirectory directory;
    const std::filesystem::path home = directory.path() / "home";
    const std::filesystem::path parent = directory.path() / "parent";
    const std::filesystem::path history = directory.path() / "history" / "one.history";
    std::filesystem::create_directories(home);
    std::filesystem::create_directories(parent);
    ::setenv("HOME", home.c_str(), 1);
    ::unsetenv("ZDOTDIR");

    for (const std::string_view name : {".zshenv", ".zprofile", ".zshrc", ".zlogin", ".bash_profile", ".bashrc", ".profile"}) {
        std::ofstream(home / name) << "READER_LOADED=\"${READER_LOADED}" << name.substr(1) << ",\"\n";
    }

    for (const std::string_view name : {".zshenv", ".zprofile", ".zshrc", ".zlogin"}) {
        std::ofstream(parent / name) << "source \"${WORKPANE_USER_ZDOTDIR}/" << name << "\"\n";
    }

    NativePseudoTerminals host;
    // clang-format off
    host.listen([]() {});
    const auto answered = [&host, &directory](const std::filesystem::path& shell, const std::filesystem::path& file, const std::string& expected) {
        auto terminal = host.start(ui::TerminalLaunch{directory.path(), 100, 30, shell, file});
        std::string output;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);

        if (!terminal.hasValue()) {
            return "not started: " + terminal.error().code + " " + terminal.error().detail;
        }

        if (!terminal.value()->write("echo LOADED=$READER_LOADED:$ZDOTDIR:$HISTFILE:END\r")) {
            return "not written, ended with " + std::to_string(terminal.value()->exitCode().value_or(-1)) + ": " + terminal.value()->takeOutput(4096);
        }

        while (output.find(expected) == std::string::npos && std::chrono::steady_clock::now() < deadline) {
            output += terminal.value()->takeOutput(4096);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        return output;
    };
    // clang-format on
#if defined(__APPLE__)
    const std::string zsh = "LOADED=zshenv,zprofile,zshrc,zlogin,::";
    const std::string zshHistory = (home / ".zsh_history").string();
    const std::string bash = "LOADED=bash_profile,::";
#else
    const std::string zsh = "LOADED=zshenv,zshrc,::";
    const std::string zshHistory;
    const std::string bash = "LOADED=bashrc,::";
#endif
    const std::vector<std::tuple<std::filesystem::path, std::filesystem::path, std::string>> cases{{"/bin/bash", history, bash + history.string() + ":END"}, {"/bin/bash", {}, bash + (home / ".bash_history").string() + ":END"}, {"/bin/zsh", history, zsh + history.string() + ":END"}, {"/bin/zsh", {}, zsh + zshHistory + ":END"}};

    for (const bool nested : {false, true}) {
        if (nested) {
            ::setenv("ZDOTDIR", parent.c_str(), 1);
            ::setenv("WORKPANE_ZDOTDIR", parent.c_str(), 1);
            ::setenv("WORKPANE_USER_ZDOTDIR", home.c_str(), 1);
            ::setenv("WORKPANE_HISTORY_FILE", (parent / "parent.history").c_str(), 1);
            ::setenv("HISTFILE", (parent / "parent.history").c_str(), 1);
        }

        for (const auto& [shell, file, expected] : cases) {
            if (!std::filesystem::exists(shell)) {
                continue;
            }

            const std::string output = answered(shell, file, expected);
            EXPECT_NE(output.find(expected), std::string::npos) << output;
            EXPECT_EQ(output.find("recursion"), std::string::npos) << output;
        }
    }
}
#endif

#if defined(_WIN32)
// A POSIX shell named on Windows, such as Git Bash, starts as a login shell and reads the profile of the reader, as Windows Terminal starts it.
TEST(NativePseudoTerminals, StartsAPosixShellOfWindowsAsALoginShell) {
    const std::filesystem::path bash = "C:\\Program Files\\Git\\bin\\bash.exe";

    if (!std::filesystem::exists(bash)) {
        return;
    }

    tests::TemporaryDirectory directory;
    const std::filesystem::path home = directory.path() / "home";
    std::filesystem::create_directories(home);
    ASSERT_EQ(_wputenv_s(L"HOME", home.wstring().c_str()), 0);
    std::ofstream(home / ".bash_profile") << "READER_LOADED=profile\n";
    auto host = std::make_unique<NativePseudoTerminals>();
    // clang-format off
    host->listen([]() {});
    // clang-format on
    auto terminal = host->start(ui::TerminalLaunch{directory.path(), 100, 30, bash, {}});
    ASSERT_TRUE(terminal.hasValue()) << terminal.error().code;
    ASSERT_TRUE(terminal.value()->write("echo LOADED=$READER_LOADED:END\r"));
    std::string output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);

    while (output.find("LOADED=profile:END") == std::string::npos && std::chrono::steady_clock::now() < deadline) {
        output += terminal.value()->takeOutput(4096);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_NE(output.find("LOADED=profile:END"), std::string::npos) << output;

    // The terminal and its host end within the grace of the shell and the bound of the last output, whatever the programs of the shell still hold.
    const auto closing = std::chrono::steady_clock::now();
    terminal.value().reset();
    host.reset();
    EXPECT_LT(std::chrono::steady_clock::now() - closing, std::chrono::seconds(10));
}

// PowerShell keeps the commands of a terminal in the history file of that terminal, so every terminal recalls its own commands.
TEST(NativePseudoTerminals, KeepsTheHistoryOfPowerShellInTheFileOfItsTerminal) {
    tests::TemporaryDirectory directory;
    const std::filesystem::path history = directory.path() / "terminal.history";
    auto host = std::make_unique<NativePseudoTerminals>();
    // clang-format off
    host->listen([]() {});
    // clang-format on
    auto terminal = host->start(ui::TerminalLaunch{directory.path(), 100, 30, {}, history});
    ASSERT_TRUE(terminal.hasValue()) << terminal.error().code;

    if (host->shellProgram().stem() == "cmd") {
        return;
    }

    ASSERT_TRUE(terminal.value()->write("echo workpane-remembered\r"));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    std::string kept;

    while (kept.find("echo workpane-remembered") == std::string::npos && std::chrono::steady_clock::now() < deadline) {
        std::ignore = terminal.value()->takeOutput(65536);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::ifstream file(history, std::ios::binary);
        kept.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    EXPECT_NE(kept.find("echo workpane-remembered"), std::string::npos);
    terminal.value().reset();
    host.reset();
}
#endif

// Collects what a real program writes and how it ends, from the threads that serve it, in a process that ignores broken pipes as the product does from its start.
class NativeProcessesTest : public ::testing::Test {
  protected:
    NativeProcessesTest() {
        ProcessSignals::ignoreBrokenPipes();
    }

    [[nodiscard]] process::ProcessEvents events() {
        // clang-format off
        return {
            [this](process::ProcessStream stream, std::string bytes) {
                const std::lock_guard lock(m_mutex);
                (stream == process::ProcessStream::Output ? m_output : m_error) += bytes;
                m_changed.notify_all();
            },
            [this](process::ProcessEnd end) {
                const std::lock_guard lock(m_mutex);
                m_code = end.code;
                m_crashed = end.crashed;
                m_changed.notify_all();
            },
        };
        // clang-format on
    }

#if !defined(_WIN32)
    // The processor time this whole test process spent, every thread included.
    [[nodiscard]] static double cpuSeconds() {
        rusage usage{};
        ::getrusage(RUSAGE_SELF, &usage);

        return static_cast<double>(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) + static_cast<double>(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1e6;
    }
#endif

    [[nodiscard]] bool ended(std::chrono::seconds timeout) {
        std::unique_lock lock(m_mutex);
        // clang-format off
        return m_changed.wait_for(lock, timeout, [this]() { return m_code.has_value(); });
        // clang-format on
    }

    NativeProcesses m_launcher;
    tests::TemporaryDirectory m_directory;
    std::mutex m_mutex;
    std::condition_variable m_changed;
    std::string m_output;
    std::string m_error;
    std::optional<int> m_code;
    bool m_crashed{false};
};

#if defined(_WIN32)

// The command prompt runs without a console window, writes both streams and reports the code it exits with.
TEST_F(NativeProcessesTest, RunsAProgramAndReportsItsStreamsAndItsEnd) {
    const auto prompt = m_launcher.find("cmd", {});
    ASSERT_TRUE(prompt.has_value());
    auto started = m_launcher.start({*prompt, {"/d", "/c", "echo got:%WORKPANE_PROBE%& echo oops 1>&2& exit 3"}, m_directory.path(), {{"WORKPANE_PROBE", "yes"}}, {}}, events());
    ASSERT_TRUE(started.hasValue()) << started.error().code << " " << started.error().detail;
    ASSERT_TRUE(ended(std::chrono::seconds(20)));

    EXPECT_EQ(m_output, "got:yes\r\n");
    EXPECT_EQ(m_error, "oops \r\n");
    EXPECT_EQ(m_code, 3);
}

// A program that keeps running is ended with everything it started once the grace after a stop has passed.
TEST_F(NativeProcessesTest, StopsAProgramThatKeepsRunning) {
    const auto ping = m_launcher.find("ping", {});
    ASSERT_TRUE(ping.has_value());
    auto started = m_launcher.start({*ping, {"-n", "60", "127.0.0.1"}, m_directory.path(), {}, {}}, events());
    ASSERT_TRUE(started.hasValue());
    const auto begun = std::chrono::steady_clock::now();
    started.value()->stop();

    ASSERT_TRUE(ended(std::chrono::seconds(20)));
    EXPECT_LT(std::chrono::steady_clock::now() - begun, std::chrono::seconds(10));
    EXPECT_FALSE(started.value()->write("late"));
}

// A program given an input reads that text and then the end of its input, and nothing more can be written to it.
TEST_F(NativeProcessesTest, ReadsTheInputItIsGivenAndThenItsEnd) {
    const auto prompt = m_launcher.find("cmd", {});
    ASSERT_TRUE(prompt.has_value());
    auto started = m_launcher.start({*prompt, {"/d", "/c", "more"}, m_directory.path(), {}, {}, std::string("given text\r\n")}, events());
    ASSERT_TRUE(started.hasValue());
    EXPECT_FALSE(started.value()->write("late"));
    ASSERT_TRUE(ended(std::chrono::seconds(20)));
    EXPECT_EQ(m_code, 0);
    EXPECT_NE(m_output.find("given text"), std::string::npos);
}

// A program given an empty input reads its end at once, so a program waiting for input finishes and nothing can be written to it.
TEST_F(NativeProcessesTest, StartsAProgramWithItsInputClosed) {
    const auto prompt = m_launcher.find("cmd", {});
    ASSERT_TRUE(prompt.has_value());
    auto started = m_launcher.start({*prompt, {"/d", "/c", "more"}, m_directory.path(), {}, {}, std::string()}, events());
    ASSERT_TRUE(started.hasValue());
    EXPECT_FALSE(started.value()->write("late"));
    ASSERT_TRUE(ended(std::chrono::seconds(20)));
    EXPECT_EQ(m_code, 0);
}

// A program that ends while a descendant still holds its streams is told as ended once the grace passes, since its job ends that descendant.
TEST_F(NativeProcessesTest, EndsOnceTheDescendantsHoldingItsStreamsAreEnded) {
    const auto prompt = m_launcher.find("cmd", {});
    ASSERT_TRUE(prompt.has_value());
    auto started = m_launcher.start({*prompt, {"/d", "/c", "start /b ping -n 60 127.0.0.1 & echo done"}, m_directory.path(), {}, {}, std::string()}, events());
    ASSERT_TRUE(started.hasValue());
    ASSERT_TRUE(ended(std::chrono::seconds(20)));

    EXPECT_NE(m_output.find("done"), std::string::npos);
}

#else

// A program runs without a shell of its own, reads what it is sent, sees the variables added and cleared, and reports both streams and its code.
TEST_F(NativeProcessesTest, RunsAProgramAndReportsItsStreamsAndItsEnd) {
    const auto shell = m_launcher.find("sh", {});
    ASSERT_TRUE(shell.has_value());
    auto started = m_launcher.start({*shell, {"-c", "read line; echo \"got:$line:$WORKPANE_PROBE:${HOME-cleared}\"; echo oops >&2; exit 3"}, m_directory.path(), {{"WORKPANE_PROBE", "yes"}}, {"HOME"}}, events());
    ASSERT_TRUE(started.hasValue()) << started.error().code << " " << started.error().detail;
    ASSERT_TRUE(started.value()->write("hello\n"));
    ASSERT_TRUE(ended(std::chrono::seconds(20)));

    EXPECT_EQ(m_output, "got:hello:yes:cleared\n");
    EXPECT_EQ(m_error, "oops\n");
    EXPECT_EQ(m_code, 3);
    EXPECT_FALSE(m_crashed);
    EXPECT_FALSE(started.value()->write("late\n"));

    // A program is found on the search path or in a directory a plugin adds, and a missing one or a missing directory is refused before anything starts.
    EXPECT_EQ(m_launcher.find(shell->filename().string(), {m_directory.path()}), shell);
    EXPECT_FALSE(m_launcher.find("workpane-no-such-program", {m_directory.path()}).has_value());
    EXPECT_EQ(m_launcher.start({m_directory.path() / "missing", {}, m_directory.path(), {}, {}}, events()).error().code, "process_program_missing");
    EXPECT_EQ(m_launcher.start({*shell, {}, m_directory.path() / "missing", {}, {}}, events()).error().code, "process_directory_missing");
}

// A program that ignores the request to end is ended for good once the grace has passed, together with what it started.
TEST_F(NativeProcessesTest, StopsAProgramThatIgnoresTheRequestToEnd) {
    const auto shell = m_launcher.find("sh", {});
    ASSERT_TRUE(shell.has_value());
    auto started = m_launcher.start({*shell, {"-c", "trap '' TERM; echo ready; sleep 60"}, m_directory.path(), {}, {}}, events());
    ASSERT_TRUE(started.hasValue());

    {
        std::unique_lock lock(m_mutex);
        // clang-format off
        ASSERT_TRUE(m_changed.wait_for(lock, std::chrono::seconds(20), [this]() { return m_output == "ready\n"; }));
        // clang-format on
    }

    const auto begun = std::chrono::steady_clock::now();
    started.value()->stop();

    ASSERT_TRUE(ended(std::chrono::seconds(20)));
    EXPECT_EQ(m_code, 137);
    EXPECT_TRUE(m_crashed);
    EXPECT_LT(std::chrono::steady_clock::now() - begun, std::chrono::seconds(10));
}

// A program joins its own group as it starts, so a stop that arrives at once still ends it and everything it started.
TEST_F(NativeProcessesTest, StopsTheGroupOfAProgramStoppedAsItStarts) {
    const auto shell = m_launcher.find("sh", {});
    ASSERT_TRUE(shell.has_value());
    auto started = m_launcher.start({*shell, {"-c", "sleep 61 & sleep 61"}, m_directory.path(), {}, {}}, events());
    ASSERT_TRUE(started.hasValue());
    started.value()->stop();

    ASSERT_TRUE(ended(std::chrono::seconds(20)));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    // The pattern names the program at the start of its command without spelling it, so the search never finds the shell that runs it.
    EXPECT_NE(std::system("ps -ax -o command | grep -q '^sleep 6[1]'"), 0);
}

// A program ended by a signal it raised itself is reported as crashed, with the code a shell would report for it.
TEST_F(NativeProcessesTest, ReportsAProgramThatCrashed) {
    const auto shell = m_launcher.find("sh", {});
    ASSERT_TRUE(shell.has_value());
    auto started = m_launcher.start({*shell, {"-c", "kill -SEGV $$"}, m_directory.path(), {}, {}}, events());
    ASSERT_TRUE(started.hasValue());

    ASSERT_TRUE(ended(std::chrono::seconds(20)));
    EXPECT_EQ(m_code, 139);
    EXPECT_TRUE(m_crashed);
}

// A program the system reaped on its own, as it does while the process ignores the end of its children, ends with no code it could tell instead of a false success.
TEST_F(NativeProcessesTest, ReportsAnEndItCouldNotReadAsACrash) {
    const auto shell = m_launcher.find("sh", {});
    ASSERT_TRUE(shell.has_value());
    const auto previous = ::signal(SIGCHLD, SIG_IGN);
    auto started = m_launcher.start({*shell, {"-c", "exit 3"}, m_directory.path(), {}, {}}, events());
    ASSERT_TRUE(started.hasValue());

    ASSERT_TRUE(ended(std::chrono::seconds(20)));
    ::signal(SIGCHLD, previous);
    EXPECT_EQ(m_code, -1);
    EXPECT_TRUE(m_crashed);
}

// A program receives its three streams alone, even a descriptor another part of the process left open to programs.
TEST_F(NativeProcessesTest, StartsAProgramWithItsThreeStreamsAlone) {
    std::array<int, 2> leaked{};
    ASSERT_EQ(::pipe(leaked.data()), 0);
    const auto shell = m_launcher.find("sh", {});
    ASSERT_TRUE(shell.has_value());
    const std::string probe = "for fd in " + std::to_string(leaked[0]) + " " + std::to_string(leaked[1]) + "; do [ -e /dev/fd/$fd ] && echo open $fd; done; echo done";
    auto started = m_launcher.start({*shell, {"-c", probe}, m_directory.path(), {}, {}, std::string()}, events());
    ASSERT_TRUE(started.hasValue());
    ASSERT_TRUE(ended(std::chrono::seconds(20)));
    ::close(leaked[0]);
    ::close(leaked[1]);

    EXPECT_EQ(m_output, "done\n");
}

// A program given an input reads that text and then the end of its input, and nothing more can be written to it.
TEST_F(NativeProcessesTest, ReadsTheInputItIsGivenAndThenItsEnd) {
    const auto shell = m_launcher.find("sh", {});
    ASSERT_TRUE(shell.has_value());
    const std::string given = "first line\nsecond line with a long tail " + std::string(200000, 'x') + "\n";
    auto started = m_launcher.start({*shell, {"-c", "cat; echo end"}, m_directory.path(), {}, {}, given}, events());
    ASSERT_TRUE(started.hasValue());
    EXPECT_FALSE(started.value()->write("late\n"));
    ASSERT_TRUE(ended(std::chrono::seconds(20)));
    EXPECT_EQ(m_code, 0);
    EXPECT_EQ(m_output, given + "end\n");
}

// A program given an empty input reads its end at once, so a program waiting for input finishes and nothing can be written to it.
TEST_F(NativeProcessesTest, StartsAProgramWithItsInputClosed) {
    const auto shell = m_launcher.find("sh", {});
    ASSERT_TRUE(shell.has_value());
    auto started = m_launcher.start({*shell, {"-c", "cat; echo done"}, m_directory.path(), {}, {}, std::string()}, events());
    ASSERT_TRUE(started.hasValue());
    EXPECT_FALSE(started.value()->write("late\n"));
    ASSERT_TRUE(ended(std::chrono::seconds(20)));
    EXPECT_EQ(m_output, "done\n");
    EXPECT_EQ(m_code, 0);
}

// A program starts with the signals the product ignores for itself set back, so a pipeline ends its writer the way a shell expects.
TEST_F(NativeProcessesTest, StartsProgramsWithTheSignalsOfAShell) {
    std::ignore = ::signal(SIGPIPE, SIG_IGN);
    const auto shell = m_launcher.find("sh", {});
    ASSERT_TRUE(shell.has_value());
    auto started = m_launcher.start({*shell, {"-c", "yes | head -n 1"}, m_directory.path(), {}, {}, std::string()}, events());
    ASSERT_TRUE(started.hasValue());
    ASSERT_TRUE(ended(std::chrono::seconds(20)));

    EXPECT_EQ(m_output, "y\n");
    EXPECT_EQ(m_error, "");
    EXPECT_EQ(m_code, 0);
}

// A program that closes its input is served without spinning while it runs, and what is written after that is refused.
TEST_F(NativeProcessesTest, ServesAProgramThatClosedItsInput) {
    const auto shell = m_launcher.find("sh", {});
    ASSERT_TRUE(shell.has_value());
    auto started = m_launcher.start({*shell, {"-c", "exec 0<&-; echo closed; sleep 1; echo done"}, m_directory.path(), {}, {}}, events());
    ASSERT_TRUE(started.hasValue());

    {
        std::unique_lock lock(m_mutex);
        // clang-format off
        ASSERT_TRUE(m_changed.wait_for(lock, std::chrono::seconds(20), [this]() { return m_output == "closed\n"; }));
        // clang-format on
    }

    const double before = cpuSeconds();
    bool refused = false;

    for (int attempt = 0; attempt < 100 && !refused; ++attempt) {
        refused = !started.value()->write("late\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ASSERT_TRUE(ended(std::chrono::seconds(20)));
    EXPECT_TRUE(refused);
    EXPECT_EQ(m_output, "closed\ndone\n");
    EXPECT_LT(cpuSeconds() - before, 0.5);
}

// A stopped program ends once the grace has passed even while a descendant that left its group still holds its streams.
TEST_F(NativeProcessesTest, StopsAProgramWhoseDescendantLeftItsGroup) {
    const auto shell = m_launcher.find("sh", {});
    const auto perl = m_launcher.find("perl", {});
    ASSERT_TRUE(shell.has_value());
    ASSERT_TRUE(perl.has_value());
    auto started = m_launcher.start({*shell, {"-c", "trap '' TERM; " + perl->string() + " -e 'setpgrp(0, 0); sleep 30' & echo ready; sleep 60"}, m_directory.path(), {}, {}}, events());
    ASSERT_TRUE(started.hasValue());

    {
        std::unique_lock lock(m_mutex);
        // clang-format off
        ASSERT_TRUE(m_changed.wait_for(lock, std::chrono::seconds(20), [this]() { return m_output == "ready\n"; }));
        // clang-format on
    }

    const auto begun = std::chrono::steady_clock::now();
    started.value()->stop();

    ASSERT_TRUE(ended(std::chrono::seconds(20)));
    EXPECT_LT(std::chrono::steady_clock::now() - begun, std::chrono::seconds(10));
}

#endif

// A page posts the address the reader opens in a background tab and the icon it draws, and only well formed posts of web addresses and PNG icons reach the owner.
TEST(WebViewPage, TakesOnlyWellFormedPostsOfItsPage) {
    platform::WebViewTracker tracker;
    tests::WebViewProbe view(tracker);
    std::vector<std::pair<std::string, bool>> opened;
    std::vector<ui::WebNavigation> navigations;
    // clang-format off
    view.setOpenHandler([&opened](std::string url, bool background) { opened.emplace_back(std::move(url), background); });
    view.setNavigationHandler([&navigations](ui::WebNavigation navigation) { navigations.push_back(std::move(navigation)); });
    // clang-format on
    view.arrive({"https://example.com/a", "A", {}, false, false, false});
    ASSERT_EQ(navigations.size(), 1U);

    view.post(R"({"kind":"open","url":"https://example.com/b"})");
    view.post(R"({"kind":"open","url":"file:///etc/hosts"})");
    view.post(R"({"kind":"open"})");
    view.post("not json");
    view.post(R"({"kind":"other","url":"https://example.com/c"})");
    EXPECT_EQ(opened, (std::vector<std::pair<std::string, bool>>{{"https://example.com/b", true}}));
    EXPECT_TRUE(tracker.takeActivity());

    view.post(R"({"kind":"icon","data":"data:text/html;base64,PGh0bWw+"})");
    view.post(R"({"kind":"icon","data":"data:image/png;base64,)" + std::string(70000, 'A') + R"("})");
    EXPECT_EQ(navigations.size(), 1U);
    view.post(R"({"kind":"icon","data":"data:image/png;base64,iVBORw0K"})");
    ASSERT_EQ(navigations.size(), 2U);
    EXPECT_EQ(navigations.back().icon, "data:image/png;base64,iVBORw0K");
    EXPECT_EQ(navigations.back().url, "https://example.com/a");

    // A burst of opens beyond what a reader clicks in a second is dropped, as a page reaching the channel could post them without end.
    opened.clear();

    for (int index = 0; index < 20; ++index) {
        view.post(R"({"kind":"open","url":"https://example.com/burst"})");
    }

    EXPECT_GE(opened.size(), 3U);
    EXPECT_LE(opened.size(), 4U);

    // The icon stays with the pages of its site and leaves with the first page of another one.
    view.arrive({"https://example.com/d", "D", {}, false, true, false});
    EXPECT_EQ(navigations.back().icon, "data:image/png;base64,iVBORw0K");
    view.arrive({"https://other.example/", "Other", {}, true, true, false});
    EXPECT_TRUE(navigations.back().icon.empty());
}

// A page opens a window only while the owner of its view takes windows, a window adopted later reports where it stands at once and a page asking to close tells its owner.
TEST(WebViewPage, OffersTheWindowsItsPagesOpen) {
    platform::WebViewTracker tracker;
    tests::WebViewProbe view(tracker);
    std::unique_ptr<ui::NativeWebView> popup;
    int closes = 0;
    EXPECT_FALSE(view.openWindow());

    // clang-format off
    view.setPopupHandler([&popup](std::unique_ptr<ui::NativeWebView> opened) { popup = std::move(opened); });
    view.setCloseHandler([&closes]() { ++closes; });
    // clang-format on
    ASSERT_TRUE(view.openWindow());
    ASSERT_NE(popup, nullptr);
    EXPECT_TRUE(tracker.takeActivity());

    auto& window = static_cast<tests::WebViewProbe&>(*popup);
    window.arrive({"https://accounts.example/sign-in", "Sign in", {}, true, false, false});
    std::vector<std::string> seen;
    // clang-format off
    popup->setNavigationHandler([&seen](ui::WebNavigation navigation) { seen.push_back(navigation.url); });
    // clang-format on
    EXPECT_EQ(seen, std::vector<std::string>{"https://accounts.example/sign-in"});

    view.askToClose();
    EXPECT_EQ(closes, 1);
}

// A page asking for the camera or the microphone waits for the owner of its view under a number, the answer reaches it once, a view nobody owns refuses at once and a view that goes refuses what still waits.
TEST(WebViewPage, AsksItsOwnerBeforeAPageUsesTheCameraOrTheMicrophone) {
    platform::WebViewTracker tracker;
    tests::WebViewProbe view(tracker);
    std::vector<std::string> answers;
    // clang-format off
    const auto record = [&answers](std::string name) { return [&answers, name](bool allowed) { answers.push_back(name + (allowed ? ":allowed" : ":refused")); }; };
    // clang-format on

    view.question("https://meet.example/room/7", true, true, record("unowned"));
    EXPECT_EQ(answers, std::vector<std::string>{"unowned:refused"});

    std::vector<ui::WebPermission> asked;
    // clang-format off
    view.setPermissionHandler([&asked](ui::WebPermission permission) { asked.push_back(std::move(permission)); });
    // clang-format on
    view.question("https://meet.example/room/7", true, true, record("call"));
    view.question("https://voice.example/", false, true, record("voice"));
    ASSERT_EQ(asked.size(), 2U);
    EXPECT_EQ(asked[0].origin, "https://meet.example");
    EXPECT_TRUE(asked[0].camera);
    EXPECT_TRUE(asked[0].microphone);
    EXPECT_FALSE(asked[1].camera);
    EXPECT_TRUE(tracker.takeActivity());

    view.answerPermission(asked[0].request, true);
    view.answerPermission(asked[0].request, false);
    view.answerPermission(99, true);
    EXPECT_EQ(answers, (std::vector<std::string>{"unowned:refused", "call:allowed"}));

    view.forgetQuestions();
    view.answerPermission(asked[1].request, true);
    EXPECT_EQ(answers, (std::vector<std::string>{"unowned:refused", "call:allowed", "voice:refused"}));
}

// A view built in the background reports once that it could not be built, to an owner that listens later as well, and asks for a frame when it tells one.
TEST(WebViewPage, ReportsOnceThatItCouldNotBeBuilt) {
    platform::WebViewTracker tracker;
    tests::WebViewProbe view(tracker);
    std::vector<std::string> failures;
    view.breakDown({"webview_create_failed", "WebView2 could not build the page", "HRESULT 0x80070002"});
    EXPECT_FALSE(tracker.takeActivity());

    // clang-format off
    view.setFailureHandler([&failures](const Error& error) { failures.push_back(error.code); });
    // clang-format on
    EXPECT_EQ(failures, std::vector<std::string>{"webview_create_failed"});

    view.breakDown({"webview_other", "Another failure", {}});
    EXPECT_EQ(failures, std::vector<std::string>{"webview_create_failed"});

    tests::WebViewProbe listened(tracker);
    // clang-format off
    listened.setFailureHandler([&failures](const Error& error) { failures.push_back(error.code); });
    // clang-format on
    listened.breakDown({"webview_create_failed", "WebView2 could not build the page", {}});
    EXPECT_EQ(failures.size(), 2U);
    EXPECT_TRUE(tracker.takeActivity());
}

// A web view cuts out the windows the product drew over it, once for each change that then asks for a frame, answers no pointer inside them and gives them back when they close.
TEST(WebViewPlacement, CutsTheWindowsDrawnOverAView) {
    platform::WebViewTracker tracker;
    tests::WebViewProbe view(tracker);
    const ImRect bounds(100.0F, 100.0F, 500.0F, 400.0F);
    const std::vector<ImRect> menu{ImRect(450.0F, 350.0F, 600.0F, 450.0F), ImRect(0.0F, 0.0F, 50.0F, 50.0F)};

    view.place(bounds, true);
    tracker.endFrame(menu);
    ASSERT_EQ(view.cuts.size(), 1U);
    ASSERT_EQ(view.cuts[0].size(), 1U);
    EXPECT_EQ(view.cuts[0][0].Min, ImVec2(350.0F, 250.0F));
    EXPECT_EQ(view.cuts[0][0].Max, ImVec2(400.0F, 300.0F));
    EXPECT_FALSE(view.contains(ImVec2(460.0F, 360.0F)));
    EXPECT_TRUE(view.contains(ImVec2(200.0F, 200.0F)));
    EXPECT_TRUE(tracker.takeActivity());

    view.place(bounds, true);
    tracker.endFrame(menu);
    EXPECT_EQ(view.cuts.size(), 1U);
    EXPECT_FALSE(tracker.takeActivity());

    view.place(bounds, true);
    tracker.endFrame({});
    ASSERT_EQ(view.cuts.size(), 2U);
    EXPECT_TRUE(view.cuts[1].empty());
    EXPECT_TRUE(view.contains(ImVec2(460.0F, 360.0F)));

    // A view nobody placed in the frame hides, and nothing is cut from a hidden view.
    tracker.endFrame(menu);
    EXPECT_EQ(view.cuts.size(), 2U);
    EXPECT_FALSE(view.contains(ImVec2(200.0F, 200.0F)));
}

} // namespace workpane::platform
