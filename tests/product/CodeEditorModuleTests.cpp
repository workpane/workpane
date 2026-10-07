#include "support/Resources.h"
#include "support/ScriptHarness.h"
#include "support/TemporaryDirectory.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <tuple>

namespace workpane::tests {

using nlohmann::json;

// Loads the Lua modules of the Code Editor plugin by themselves and checks the rules they carry, which the product tests reach only through a few cases.
class CodeEditorModuleTest : public ::testing::Test {
  protected:
    // Each module is loaded in an environment that answers include with the other modules of the plugin and names texts by their keys, and a body that waits is run until it reports.
    [[nodiscard]] static json run(std::string_view body) {
        const std::string directory = (Resources::staged() / "plugins" / "code-editor").generic_string();
        const std::string prelude = R"(
            local directory = [==[)" +
                                    directory + R"(]==]
            local loaded = {}
            local environment = setmetatable({}, { __index = _G })
            local paths = require("workpane.paths")
            local system = require("platform").os()
            environment.workpane = { ui = {}, i18n = { text = function(key) return key end, number = function(value) return value end, translate = function(key) return key end } }
            environment.workpane.files = { uri = paths.uri, path = function(uri) return paths.path(system, uri) end, absolute = function(path) return paths.absolute(system, path) end }

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
        // clang-format off
        std::ignore = harness.pollUntil([&harness]() { return !harness.results.empty(); });
        // clang-format on
        EXPECT_EQ(harness.results.size(), 1U) << json(harness.logged).dump();

        return harness.results.size() == 1 ? harness.results.front() : json();
    }
};

// Sections follow the EditorConfig globs: stars stop at separators, double stars cross them or stand for no folder, classes, alternatives, ranges and escapes read as the specification says, and braces without a comma are literal.
TEST_F(CodeEditorModuleTest, MatchesEditorConfigSections) {
    const json results = run(R"(
        local editorconfig = include("editorconfig")
        local cases = {
            { "*", "a.txt" }, { "*", "dir/a.txt" }, { "*.txt", "a.lua" }, { "*.{js,py}", "src/x.py" }, { "*.{js,py}", "x.rb" },
            { "/root.txt", "root.txt" }, { "/root.txt", "dir/root.txt" }, { "lib/**.js", "lib/a/b/c.js" }, { "lib/*.js", "lib/a/b.js" },
            { "a/**/b.txt", "a/x/y/b.txt" }, { "[abc].txt", "b.txt" }, { "[!abc].txt", "b.txt" }, { "[!abc].txt", "d.txt" },
            { "file{1..3}.txt", "file2.txt" }, { "file{1..3}.txt", "file4.txt" }, { "\\*.txt", "*.txt" }, { "\\*.txt", "a.txt" },
            { "?.txt", "a.txt" }, { "?.txt", "ab.txt" }, { "{a,{b,c}}.txt", "c.txt" }, { "{a,b.txt", "{a,b.txt" },
            { "a/**/b.txt", "a/b.txt" }, { "{single}.txt", "{single}.txt" }, { "{single}.txt", "single.txt" }, { "{}.txt", "{}.txt" },
        }

        local answers = {}

        for index, case in ipairs(cases) do
            answers[index] = editorconfig.matches(case[1], case[2])
        end

        host.test_result({ answers = answers })
    )");

    EXPECT_EQ(results["answers"], json::array({true, true, false, true, false, true, false, true, false, true, true, false, true, true, false, true, false, true, false, true, true, true, true, false, true}));
}

// An indent size of tab takes the width of a tab, four when none is set, and every width stays within the one to sixteen columns the editor draws.
TEST_F(CodeEditorModuleTest, ResolvesIndentWidthsTheEditorDraws) {
    TemporaryDirectory folder;
    const std::map<std::string, std::string> files{{"tab", "root = true\n[*]\nindent_size = tab\n"}, {"tabbed", "root = true\n[*]\nindent_style = tab\nindent_size = tab\ntab_width = 3\n"}, {"wide", "root = true\n[*]\nindent_style = space\nindent_size = 40\n"}, {"narrow", "root = true\n[*]\nindent_style = tab\ntab_width = 99\n"}};

    for (const auto& [name, text] : files) {
        std::filesystem::create_directories(folder.path() / name);
        std::ofstream(folder.path() / name / ".editorconfig") << text;
    }

    const json results = run(R"(
        local root = [==[)" + folder.path().generic_string() +
                             R"(]==]
        require("workpane.task").run("test", "task", function()
            local editorconfig = include("editorconfig")
            local widths = {}
            for _, name in ipairs({ "tab", "tabbed", "wide", "narrow" }) do
                widths[name] = editorconfig.resolve(root .. "/" .. name .. "/a.txt", root .. "/" .. name).indentWidth
            end
            host.test_result(widths)
        end)
    )");

    EXPECT_EQ(results["tab"], 4);
    EXPECT_EQ(results["tabbed"], 3);
    EXPECT_EQ(results["wide"], 16);
    EXPECT_EQ(results["narrow"], 16);
}

// A document waiting for the text of its editor goes on once it closes, and a closed document never asks its editor again, since a closed editor never answers.
TEST_F(CodeEditorModuleTest, LetsGoOfTheWaitsOfAClosedDocument) {
    const json results = run(R"(
        local task = require("workpane.task")
        environment.workpane.plugin = { directory = directory }
        task.run("test", "task", function()
            include("catalog").load()
            local documents = include("documents")
            local document = documents.new(nil, "/folder/notes.txt", nil, {})
            local flushes = 0
            local waited = false
            document.node = { command = function() flushes = flushes + 1 end, set = function() end }
            task.run("test", "waiter", function()
                document:settle()
                waited = true
            end)
            require("async").sleep(10):await()
            local before = waited
            document:close()
            require("async").sleep(10):await()
            document:settle()
            document:poll()
            host.test_result({ before = before, after = waited, flushes = flushes, closed = document.closed })
        end)
    )");

    EXPECT_EQ(results["before"], false);
    EXPECT_EQ(results["after"], true);
    EXPECT_EQ(results["flushes"], 1);
    EXPECT_EQ(results["closed"], true);
}

// Every mark, the fallback encoding and the refused files decode as each charset defines them, and text is written back in each charset with the EditorConfig rules.
TEST_F(CodeEditorModuleTest, DecodesAndEncodesTheCharsetsItKnows) {
    const json results = run(R"(
        local encoding = include("encoding")
        local function decoded(bytes, manual)
            local answer, failure = encoding.decode(bytes, manual, "latin1")
            return answer ~= nil and { text = answer.text, charset = answer.charset, lineEnding = answer.lineEnding } or { failure = failure.code, detail = failure.detail }
        end

        host.test_result({
            latin = decoded("caf\xE9\n"),
            crlf = decoded("a\r\nb"),
            cr = decoded("a\rb"),
            bom = decoded("\xEF\xBB\xBFhi"),
            wide = decoded("\xFE\xFF\0h\0i"),
            pair = decoded("\xFF\xFE\x3D\xD8\x00\xDE"),
            lone = decoded("\xFF\xFE\x3D\xD8"),
            binary = decoded("\0\1"),
            utf32 = decoded("\xFF\xFE\0\0h\0\0\0"),
            utf7 = decoded("+/v8"),
            manual = decoded("caf\xE9", "utf-8"),
            trimmed = encoding.encode("x  \ny", "utf-8", "crlf", { trimTrailingWhitespace = true, insertFinalNewline = true }),
            kept = encoding.encode("x  \n", "utf-8", "lf", {}),
            narrow = encoding.encode("\xC3\xA9", "latin1", "lf", {}) == "\xE9",
            refused = encoding.encode("\xF0\x9F\x98\x80", "latin1", "lf", {}) == nil,
            surrogates = encoding.encode("\xF0\x9F\x98\x80", "utf-16le", "lf", {}) == "\xFF\xFE\x3D\xD8\x00\xDE",
            marked = encoding.encode("hi", "utf-8-bom", "lf", {}) == "\xEF\xBB\xBFhi",
        })
    )");

    EXPECT_EQ(results["latin"], (json{{"text", "caf\xC3\xA9\n"}, {"charset", "latin1"}, {"lineEnding", "lf"}}));
    EXPECT_EQ(results["crlf"], (json{{"text", "a\nb"}, {"charset", "utf-8"}, {"lineEnding", "crlf"}}));
    EXPECT_EQ(results["cr"], (json{{"text", "a\nb"}, {"charset", "utf-8"}, {"lineEnding", "cr"}}));
    EXPECT_EQ(results["bom"], (json{{"text", "hi"}, {"charset", "utf-8-bom"}, {"lineEnding", "lf"}}));
    EXPECT_EQ(results["wide"], (json{{"text", "hi"}, {"charset", "utf-16be"}, {"lineEnding", "lf"}}));
    EXPECT_EQ(results["pair"], (json{{"text", "\xF0\x9F\x98\x80"}, {"charset", "utf-16le"}, {"lineEnding", "lf"}}));
    EXPECT_EQ(results["lone"]["failure"], "code-editor.error.encoding");
    EXPECT_EQ(results["binary"]["failure"], "code-editor.error.binary-file");
    EXPECT_EQ(results["utf32"], (json{{"failure", "code-editor.error.encoding-unsupported"}, {"detail", " UTF-32"}}));
    EXPECT_EQ(results["utf7"], (json{{"failure", "code-editor.error.encoding-unsupported"}, {"detail", " UTF-7"}}));
    EXPECT_EQ(results["manual"]["failure"], "code-editor.error.encoding");
    EXPECT_EQ(results["trimmed"], "x\r\ny\r\n");
    EXPECT_EQ(results["kept"], "x  \n");
    EXPECT_EQ(results["narrow"], true);
    EXPECT_EQ(results["refused"], true);
    EXPECT_EQ(results["surrogates"], true);
    EXPECT_EQ(results["marked"], true);
}

// A match counts one more for every character that follows the one before it, a match ending in the file name counts twice, and equal scores keep the order of the walk.
TEST_F(CodeEditorModuleTest, RanksFilesByTheCharactersOfTheQuery) {
    const json results = run(R"(
        local finder = include("views/finder")
        host.test_result({
            empty = finder.score("src/main.lua", ""),
            missing = finder.score("src/main.lua", "xyz") == nil,
            name = finder.score("src/main.lua", "main"),
            folder = finder.score("main/src.lua", "main"),
            gap = finder.score("src/main.lua", "sml"),
            ranked = finder.rank({ "main/src.lua", "src/main.lua", "docs/readme.md" }, "main", 10),
            walked = finder.rank({ "b.txt", "a.txt", "c.txt" }, "", 2),
        })
    )");

    EXPECT_EQ(results["empty"], 0);
    EXPECT_EQ(results["missing"], true);
    EXPECT_EQ(results["name"], 20);
    EXPECT_EQ(results["folder"], 10);
    EXPECT_EQ(results["gap"], 6);
    EXPECT_EQ(results["ranked"], json::array({"src/main.lua", "main/src.lua"}));
    EXPECT_EQ(results["walked"], json::array({"b.txt", "a.txt"}));
}

// A server that never answers initialization is ended and started again within the restart budget, and one that cannot be started again is given up with its reason.
TEST_F(CodeEditorModuleTest, RestartsAServerThatDoesNotAnswerInitialization) {
    const json results = run(R"(
        loaded.catalog = { limit = function(name) return ({ initializeTimeoutMs = 20, maximumRestarts = 2, restartWindowMs = 60000 })[name] end }
        local run = function(work, ...) require("workpane.task").run("test", "task", work, ...) end
        local starts = 0
        local failures = {}
        local running = {}
        environment.workpane.app = { processId = 7 }
        environment.workpane.task = run
        environment.workpane.process = {
            start = function(options)
                starts = starts + 1
                running[starts] = options
                return starts
            end,
            write = function() end,
            stop = function(id)
                run(function() running[id].onExit(143) end)
            end,
        }

        run(function()
            local lsp = include("lsp")
            local handlers = { failed = function(message) failures[#failures + 1] = message end, log = function() end, ready = function() end, stopped = function() end, notification = function() end, capabilities = function() end }
            lsp.new("/work", "lua", "lua-language-server", {}, handlers):start()

            for _ = 1, 100 do
                if #failures > 0 then
                    break
                end

                require("async").sleep(10):await()
            end

            host.test_result({ starts = starts, failures = failures })
        end)
    )");

    EXPECT_EQ(results["starts"], 3);
    ASSERT_EQ(results["failures"].size(), 1U);
    EXPECT_EQ(results["failures"][0]["key"], "code-editor.lsp.restart-limit");
    EXPECT_NE(results["failures"][0]["detail"].get<std::string>().find("restart limit"), std::string::npos);
}

// Frames split across batches and several frames in one batch are read in the order they came, a handler that waits holds no frame after it and one that fails holds none either.
TEST_F(CodeEditorModuleTest, ReadsTheFramesOfALanguageServerInTheOrderTheyCame) {
    const json results = run(R"(
        loaded.catalog = { limit = function(name) return ({ initializeTimeoutMs = 60000, maximumRestarts = 2, restartWindowMs = 60000 })[name] end }
        local run = function(work, ...) require("workpane.task").run("test", "task", work, ...) end
        local process
        local logged = {}
        environment.workpane.app = { processId = 7 }
        environment.workpane.task = run
        environment.workpane.log = { error = function() end }
        environment.workpane.process = { start = function(options) process = options return 1 end, write = function() end, stop = function() end }

        run(function()
            local seen = {}
            local handlers = { failed = function() end, ready = function() end, stopped = function() end, log = function(line) logged[#logged + 1] = line end, notification = function(_, params)
                if params.wait then
                    require("async").sleep(20):await()
                end

                if params.fail then
                    error("broken")
                end

                seen[#seen + 1] = params.index
            end }

            include("lsp").new("/work", "lua", "server", {}, handlers):start()

            local function frame(index, extra)
                local body = '{"jsonrpc":"2.0","method":"note","params":{"index":' .. index .. (extra or "") .. '}}'
                return "Content-Length: " .. #body .. "\r\n\r\n" .. body
            end

            local first = frame(1, ',"wait":true') .. frame(2)
            local second = frame(3, ',"fail":true') .. frame(4)
            process.onOutput({ { stream = "output", text = first:sub(1, 10) }, { stream = "output", text = first:sub(11) .. second:sub(1, 7) } })
            process.onOutput({ { stream = "output", text = second:sub(8) }, { stream = "error", text = "line one\nline " }, { stream = "output", text = frame(5) }, { stream = "error", text = "two\n" } })
            require("async").sleep(60):await()
            host.test_result({ seen = seen, logged = logged })
        end)
    )");

    EXPECT_EQ(results["seen"], json::array({2, 4, 5, 1}));
    EXPECT_EQ(results["logged"], json::array({"line one", "line two"}));
}

// A file reaches its server under the identifier the protocol gives its extension, such as C among C++ files and TSX among TypeScript files.
TEST_F(CodeEditorModuleTest, NamesEachFileTheWayItsServerKnowsIt) {
    const json results = run(R"(
        environment.workpane.plugin = { directory = directory }
        require("workpane.task").run("test", "task", function()
            local catalog = include("catalog")
            catalog.load()
            local ids = {}

            for _, path in ipairs({ "/src/app.tsx", "/src/app.ts", "/src/view.jsx", "/src/view.js", "/src/main.c", "/src/MAIN.C", "/src/main.cpp", "/src/notes.txt" }) do
                ids[#ids + 1] = catalog.protocolId(catalog.detect(path), path)
            end

            host.test_result({ ids = ids })
        end)
    )");

    EXPECT_EQ(results["ids"], json::array({"typescriptreact", "typescript", "javascriptreact", "javascript", "c", "c", "cpp", "plaintext"}));
}

// A closed document leaves nothing behind in the analysis, and a stopped analysis starts no server for a document opened after it stopped.
TEST_F(CodeEditorModuleTest, StopsAnAnalysisForGoodAndForgetsWhatClosed) {
    const json results = run(R"(
        local run = function(work, ...) require("workpane.task").run("test", "task", work, ...) end
        local starts = 0
        loaded.catalog = { limit = function() return 60000 end, protocolId = function(language) return language.id end }
        loaded.preferences = { get = function() return true end }
        loaded.servers = { executable = function() return { path = "/bin/server", arguments = {} } end, any = function() return true end }
        environment.workpane.app = { processId = 7 }
        environment.workpane.task = run
        environment.workpane.process = { start = function() starts = starts + 1 return starts end, write = function() end, stop = function() end }

        run(function()
            local quiet = function() end
            local analysis = include("analysis").new("/work", { changed = quiet, progress = quiet, dropped = quiet, diagnostics = quiet, outline = quiet, highlights = quiet })
            local document = { path = "/work/a.lua", language = { id = "lua" }, text = "x", cursor = { line = 1, column = 1 }, line = function() return "x" end }
            analysis:open(document)
            analysis:changed(document)
            local counted = analysis.revisions[document]
            analysis:closed(document)
            local pruned = analysis.revisions[document] == nil and analysis.analyzed[document] == nil
            analysis:stop()
            analysis:open(document)
            host.test_result({ starts = starts, counted = counted, pruned = pruned, tracked = next(analysis.tracked) == nil })
        end)
    )");

    EXPECT_EQ(results["starts"], 1);
    EXPECT_EQ(results["counted"], 1);
    EXPECT_EQ(results["pruned"], true);
    EXPECT_EQ(results["tracked"], true);
}

// A read of the file that a save crossed is dropped, so the bytes it found before the save never replace the text that was saved.
TEST_F(CodeEditorModuleTest, DropsAReadThatASaveCrossed) {
    TemporaryDirectory folder;
    std::ofstream(folder.path() / "notes.txt", std::ios::binary) << "older bytes\n";

    const json results = run(R"(
        local root = [==[)" + std::filesystem::canonical(folder.path()).generic_string() +
                             R"(]==]
        local run = function(work, ...) require("workpane.task").run("test", "task", work, ...) end
        environment.workpane.plugin = { directory = directory }
        environment.workpane.notify = { error = function() end }
        loaded.preferences = { get = function() return "utf-8" end }

        -- The read waits at a gate until the save is counted, so the save crosses it however fast the disk answers.
        local async = require("async")
        local files = require("fs")
        local gate, open = async.deferred()
        local held = setmetatable({ stat = function(path) return async.promise(function() gate:await() return files.stat(path):await() end) end }, { __index = files })
        environment.require = function(name) return name == "fs" and held or require(name) end

        run(function()
            include("catalog").load()
            local changes = 0
            local document = include("documents").new({ root = root }, root .. "/notes.txt", nil, { changed = function() changes = changes + 1 end, loaded = function() end })
            document.node = { command = function() end, set = function() end }
            document.loaded = true
            document.text = "saved\n"
            document.savedText = "saved\n"
            document.digest = "saved"
            local finished = false

            run(function()
                document:read(false)
                finished = true
            end)

            document.saves = document.saves + 1
            open()

            while not finished do
                require("async").sleep(5):await()
            end

            local crossed = document.text
            local changed = changes
            document:read(false)
            host.test_result({ crossed = crossed, changed = changed, read = document.text })
        end)
    )");

    EXPECT_EQ(results["crossed"], "saved\n");
    EXPECT_EQ(results["changed"], 0);
    EXPECT_EQ(results["read"], "older bytes\n");
}

// The tree lists again only the folders on screen and forgets a folder that went away, with everything that was open inside it.
TEST_F(CodeEditorModuleTest, ListsOnlyTheFoldersOnScreenAndForgetsTheOnesThatWent) {
    const json results = run(R"(
        local run = function(work, ...) require("workpane.task").run("test", "task", work, ...) end
        local listings = { ["/work"] = { { name = "a", kind = "directory" } }, ["/work/a"] = { { name = "b", kind = "directory" } }, ["/work/a/b"] = { { name = "c.txt", kind = "file" } } }
        local listed = {}
        environment.workpane.ui = { tree = function() return { set = function() end, command = function() end } end }
        environment.workpane.files.list = function(path)
            listed[path] = (listed[path] or 0) + 1
            return { await = function() return listings[path], listings[path] == nil and { code = "files_missing" } or nil end }
        end

        run(function()
            local tree = include("views/tree").new("/work", { watched = function() end })
            tree:start()
            tree:toggled("/work/a", true)
            tree:toggled("/work/a/b", true)
            tree:toggled("/work/a", false)
            listed = {}
            tree:poll()
            local collapsed = { root = listed["/work"] or 0, inner = listed["/work/a/b"] or 0 }
            tree:toggled("/work/a", true)
            listings["/work/a"] = {}
            listings["/work/a/b"] = nil
            tree:poll()
            host.test_result({ collapsed = collapsed, forgotten = tree.folders["/work/a/b"] == nil and tree.expanded["/work/a/b"] == nil, kept = tree.folders["/work/a"] ~= nil })
        end)
    )");

    EXPECT_EQ(results["collapsed"]["root"], 1);
    EXPECT_EQ(results["collapsed"]["inner"], 0);
    EXPECT_EQ(results["forgotten"], true);
    EXPECT_EQ(results["kept"], true);
}

// Positions convert between the characters of the editor and the UTF-16 units of the protocol.
TEST_F(CodeEditorModuleTest, ConvertsPositionsForTheProtocol) {
    const json results = run(R"(
        local lsp = include("lsp")
        host.test_result({
            toUnits = lsp.toUnits("a\xF0\x9F\x98\x80b", 3),
            fromUnits = lsp.fromUnits("a\xF0\x9F\x98\x80b", 3),
            beyond = lsp.fromUnits("ab", 5),
        })
    )");

    EXPECT_EQ(results["toUnits"], 3);
    EXPECT_EQ(results["fromUnits"], 3);
    EXPECT_EQ(results["beyond"], 6);
}

} // namespace workpane::tests
