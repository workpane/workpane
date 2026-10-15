#include "app/Product.h"
#include "persistence/PreferenceStore.h"
#include "platform/NativeProcesses.h"
#include "platform/NativePseudoTerminals.h"
#include "platform/PathTextHelper.h"
#include "platform/PrivateDirectoryHelper.h"
#include "platform/ProcessSignals.h"
#include "process/Process.h"
#include "process/ProcessEnd.h"
#include "process/ProcessEvents.h"
#include "process/ProcessLaunch.h"
#include "process/ProcessStream.h"
#include "scripting/ScriptRuntime.h"
#include "support/FakeProvider.h"
#include "support/HeadlessProduct.h"
#include "support/LocalPort.h"
#include "support/ProductDatabase.h"
#include "support/Resources.h"
#include "support/ScriptHarness.h"
#include "support/SftpProbeHelper.h"
#include "support/SshScopeProbeHelper.h"
#include "support/SshTerminalProbeHelper.h"
#include "support/SurfaceReader.h"
#include "support/TemporaryDirectory.h"
#include "text/Base64Helper.h"
#include "ui/model/SurfaceStore.h"
#include "ui/shell/Shell.h"

#include <gtest/gtest.h>
#include <httplib.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>
#include <pwd.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace workpane::tests {

class RemoteSshIntegrationTest : public ::testing::Test {
  protected:
#if defined(__APPLE__)
    static constexpr std::string_view nativePlatform{"macos"};
#else
    static constexpr std::string_view nativePlatform{"linux"};
#endif

    enum class ManagerScenario { ProfileFolder, ProfileFolderCancel, ProfileFolderCredentialsCancel, Disconnect, Lost, Trust, DeclinedTrust, Password, Stop, Idle, Terminal, TerminalBash, TerminalLost, TerminalStop, Editor, EditorLanguage, EditorConflict, EditorReconnect, Ports, PortsLost, PortsStop, PortsReconnect, Task, TaskCancel, TaskLost, TaskStop, TaskAgent, TaskCli };

    struct Output final {
        std::mutex mutex;
        std::condition_variable changed;
        std::string text;
        std::optional<process::ProcessEnd> end;
    };

    void SetUp() override {
        platform::ProcessSignals::ignoreBrokenPipes();
        const auto ssh = m_launcher.find("ssh", {"/usr/bin"});
        const auto sshd = m_launcher.find("sshd", {"/usr/sbin", "/usr/lib/openssh"});
        const auto keygen = m_launcher.find("ssh-keygen", {"/usr/bin"});
        ASSERT_TRUE(ssh.has_value());
        ASSERT_TRUE(sshd.has_value());
        ASSERT_TRUE(keygen.has_value());
        m_ssh = *ssh;
        const auto* user = getpwuid(getuid());
        ASSERT_NE(user, nullptr);
        m_user = user->pw_name;
        m_directory = std::make_unique<TemporaryDirectory>(user->pw_dir);
        m_root = std::filesystem::canonical(m_directory->path());
        std::filesystem::permissions(m_root, std::filesystem::perms::owner_all);

        generateKey(*keygen, "host");
        generateKey(*keygen, "client");
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        std::filesystem::copy_file(m_root / "client.pub", m_root / "authorized_keys");
        m_port = LocalPort::free();
        ASSERT_GT(m_port, 0);
        std::ofstream configuration(m_root / "sshd_config");
        configuration << "ListenAddress 127.0.0.1\nPort " << m_port << "\nHostKey " << std::quoted(path("host")) << "\nPidFile " << std::quoted(path("sshd.pid")) << "\nAuthorizedKeysFile " << std::quoted(path("authorized_keys")) << "\nPasswordAuthentication no\nKbdInteractiveAuthentication no\nPubkeyAuthentication yes\nUsePAM no\nAllowUsers " << m_user << "\nSubsystem sftp internal-sftp\nLogLevel VERBOSE\n";
        configuration.close();
        std::ifstream publicKey(m_root / "host.pub");
        std::string algorithm;
        std::string encoded;
        publicKey >> algorithm >> encoded;
        ASSERT_FALSE(encoded.empty());
        std::ofstream(m_root / "known_hosts") << "[127.0.0.1]:" << m_port << " " << algorithm << " " << encoded << "\n";
        m_daemonOutput = std::make_shared<Output>();
        m_daemon = start(*sshd, {"-D", "-e", "-f", path("sshd_config")}, "", m_daemonOutput);
        ASSERT_NE(m_daemon, nullptr);
        std::unique_lock lock(m_daemonOutput->mutex);
        // clang-format off
        ASSERT_TRUE(m_daemonOutput->changed.wait_for(lock, std::chrono::seconds(10), [this]() { return m_daemonOutput->end.has_value() || m_daemonOutput->text.find("Server listening on") != std::string::npos; }));
        // clang-format on
        ASSERT_FALSE(m_daemonOutput->end.has_value()) << m_daemonOutput->text;
    }

    void TearDown() override {
        m_forwardServer.stop();

        if (m_forwardThread.joinable()) {
            m_forwardThread.join();
        }

        if (m_connection != nullptr) {
            m_connection->stop();
            m_connection.reset();
        }

        if (m_daemon != nullptr) {
            m_daemon->stop();
            m_daemon.reset();
        }
    }

    [[nodiscard]] std::string path(std::string_view name) const {
        return platform::PathTextHelper::generic(m_root / name);
    }

    [[nodiscard]] std::unique_ptr<process::Process> start(const std::filesystem::path& executable, std::vector<std::string> arguments, std::optional<std::string> input, const std::shared_ptr<Output>& output) {
        process::ProcessEvents events;
        // clang-format off
        events.output = [output](process::ProcessStream, std::string bytes) {
            const std::lock_guard lock(output->mutex);
            output->text.append(bytes, 0, std::min(bytes.size(), 65536U - output->text.size()));
            output->changed.notify_all();
        };

        events.exited = [output](process::ProcessEnd end) {
            const std::lock_guard lock(output->mutex);
            output->end = end;
            output->changed.notify_all();
        };
        // clang-format on

        auto started = m_launcher.start({executable, std::move(arguments), m_root, {}, {}, std::move(input)}, std::move(events));
        EXPECT_TRUE(started.hasValue()) << (started.hasValue() ? "" : started.error().code);

        return started.hasValue() ? std::move(started.value()) : nullptr;
    }

    [[nodiscard]] std::vector<std::string> arguments(std::string_view key, int proxyPort) const {
        std::vector<std::string> result{"-F", "/dev/null", "-T", "-p", std::to_string(m_port), "-i", path(key), "-o", "IdentitiesOnly=yes", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=yes", "-o", "UserKnownHostsFile=" + path("known_hosts"), "-o", "GlobalKnownHostsFile=/dev/null", "-o", "ConnectTimeout=5", "-o", "ExitOnForwardFailure=yes", "-o", "ControlMaster=no", "-o", "ControlPath=none"};

        if (proxyPort > 0) {
            result.push_back("-D");
            result.push_back("127.0.0.1:" + std::to_string(proxyPort));
        }

        result.insert(result.end(), {"-l", m_user, "127.0.0.1"});

        return result;
    }

    [[nodiscard]] const std::filesystem::path& ssh() const {
        return m_ssh;
    }

#if defined(__APPLE__)
    void verifyNativeMacSessionCleanup() {
        std::ifstream resource(Resources::staged() / "plugins" / "remote-ssh" / "assets" / "ssh-session-macos.js");
        const std::string script{std::istreambuf_iterator<char>(resource), std::istreambuf_iterator<char>()};
        ASSERT_FALSE(script.empty());
        const std::string driver = "/usr/bin/osascript -l JavaScript -e " + quoted(script);
        const std::string inspection = driver + " session \"$$\"";
        const std::string watchdog = "/bin/ps -e -o pid= | " + driver + " signal \"$session\" 9 >/dev/null 2>&1";
        const std::string body = "set -m\ntrap '' HUP INT TERM\nsession=$(" + inspection + ") || exit 1\n( sleep 30\n" + watchdog + " ) &\n/bin/sh -c 'trap \"\" HUP INT TERM; while :; do sleep 120; done' &\nchild=$!\nparentGroup=$(/bin/ps -p $$ -o pgid=)\nchildGroup=$(/bin/ps -p \"$child\" -o pgid=)\nprintf 'SESSION %s %s %s %s %s\\n' \"$session\" \"$$\" \"$child\" \"$parentGroup\" \"$childGroup\"\nwait \"$child\"\n";
        auto options = arguments("client", 0);
        options.push_back("exec /bin/sh -c " + quoted(body));
        m_connectionOutput = std::make_shared<Output>();
        m_connection = start(m_ssh, std::move(options), std::nullopt, m_connectionOutput);
        ASSERT_NE(m_connection, nullptr);
        const auto ready = line("SESSION ");
        ASSERT_TRUE(ready.has_value());
        std::istringstream declaration(*ready);
        std::string prefix;
        int session = 0;
        int owner = 0;
        int child = 0;
        int parentGroup = 0;
        int childGroup = 0;
        declaration >> prefix >> session >> owner >> child >> parentGroup >> childGroup;
        ASSERT_EQ(prefix, "SESSION");
        ASSERT_EQ(session, owner);
        ASSERT_GT(child, 1);
        EXPECT_NE(parentGroup, childGroup);

        for (const int signal : {15, 9}) {
            auto commands = arguments("client", 0);
            commands.push_back("/bin/ps -e -o pid= | " + driver + " signal " + std::to_string(session) + " " + std::to_string(signal));
            const auto output = std::make_shared<Output>();
            auto control = start(m_ssh, std::move(commands), std::nullopt, output);
            EXPECT_NE(control, nullptr);

            if (control != nullptr) {
                std::unique_lock lock(output->mutex);
                // clang-format off
                const bool finished = output->changed.wait_for(lock, std::chrono::seconds(10), [&output]() { return output->end.has_value(); });
                // clang-format on
                EXPECT_TRUE(finished) << output->text;

                if (finished) {
                    EXPECT_EQ(output->end->code, 0) << output->text;
                }
            }

            if (signal == 15) {
                EXPECT_EQ(::getsid(owner), session);
                EXPECT_EQ(::getsid(child), session);
            }
        }

        std::unique_lock lock(m_connectionOutput->mutex);
        // clang-format off
        EXPECT_TRUE(m_connectionOutput->changed.wait_for(lock, std::chrono::seconds(10), [this]() { return m_connectionOutput->end.has_value(); }));
        // clang-format on
        EXPECT_EQ(::getsid(owner), -1);
        EXPECT_EQ(::getsid(child), -1);
    }
#endif

    [[nodiscard]] static nlohmann::json module(std::string_view body) {
        const std::string directory = (Resources::staged() / "plugins" / "remote-ssh").generic_string();
        const std::string prelude = R"lua(
            local directory = [==[)lua" +
                                    directory + R"lua(]==]
            local loaded = {}
            local environment = setmetatable({}, { __index = _G })
            local paths = require("workpane.paths")
            environment.workpane = {
                plugin = { directory = directory },
                files = {
                    absolute = function(path) return paths.absolute(require("platform").os(), path) end,
                    paths = function(platform)
                        return { normalizeRoot = function(path) return paths.normalizeRoot(platform, path) end }
                    end,
                },
            }

            function environment.include(name)
                if loaded[name] == nil then
                    loaded[name] = assert(loadfile(directory .. "/" .. name .. ".lua", "t", environment))()
                end

                return loaded[name]
            end

            local async = require("async")
        )lua";
        ScriptHarness harness;
        const auto loaded = harness.run(prelude + std::string(body));
        EXPECT_TRUE(loaded.hasValue()) << (loaded.hasValue() ? "" : loaded.error().code);
        // clang-format off
        EXPECT_TRUE(harness.pollUntil([&harness]() { return !harness.results.empty(); }));
        // clang-format on
        EXPECT_TRUE(harness.logged.empty()) << nlohmann::json(harness.logged).dump();
        EXPECT_EQ(harness.results.size(), 1U);

        return harness.results.size() == 1 ? harness.results.front() : nlohmann::json();
    }

    [[nodiscard]] std::optional<std::string> line(std::string_view prefix) const {
        std::unique_lock lock(m_connectionOutput->mutex);
        // clang-format off
        const auto complete = [&]() {
            const auto begin = m_connectionOutput->text.find(prefix);
            return (begin != std::string::npos && m_connectionOutput->text.find('\n', begin) != std::string::npos) || m_connectionOutput->end.has_value();
        };
        // clang-format on

        EXPECT_TRUE(m_connectionOutput->changed.wait_for(lock, std::chrono::seconds(30), complete));
        const auto begin = m_connectionOutput->text.find(prefix);

        if (begin == std::string::npos) {
            ADD_FAILURE() << m_connectionOutput->text;
            return std::nullopt;
        }

        const auto end = m_connectionOutput->text.find('\n', begin);

        if (end == std::string::npos) {
            ADD_FAILURE() << m_connectionOutput->text;
            return std::nullopt;
        }

        return m_connectionOutput->text.substr(begin, end - begin);
    }

    void prepareSession() {
        // clang-format off
        m_forwardServer.Get("/forwarded", [](const httplib::Request&, httplib::Response& response) {
            response.set_content(std::string("\0\xff\x80", 3) + std::string(100000, 'x'), "application/octet-stream");
        });
        // clang-format on

        const int serverPort = LocalPort::bind(m_forwardServer);
        m_forwardPort = serverPort;
        ASSERT_GT(serverPort, 0);
        // clang-format off
        m_forwardThread = std::thread([this]() { std::ignore = m_forwardServer.listen_after_bind(); });
        // clang-format on

        m_forwardServer.wait_until_ready();
        const auto home = m_root / std::filesystem::path(std::u8string(u8"Session home ' café [x]*? \\ \""));
        ASSERT_FALSE(platform::PrivateDirectoryHelper::create(home));
        std::ofstream(home / ".zshrc") << "PS1='workpane-native-ready> '\n";
        std::ofstream(home / ".bashrc") << "PS1='workpane-native-ready> '\n";
        std::ofstream(home / ".bash_profile") << "PS1='workpane-native-ready> '\n";
        const auto configuration = m_root / "session_config";
        std::ofstream(configuration) << "Host workpane-test\nHostName 127.0.0.1\nUser " << m_user << "\nPort " << m_port << "\nIdentityFile " << std::quoted(path("client")) << "\nIdentitiesOnly yes\nUserKnownHostsFile " << std::quoted(path("known_hosts")) << "\nGlobalKnownHostsFile /dev/null\n";
        const auto profile = nlohmann::json{{"id", "development"}, {"name", "Development"}, {"host", "workpane-test"}, {"user", ""}, {"port", 0}, {"identity", ""}, {"configuration", platform::PathTextHelper::generic(configuration)}, {"root", platform::PathTextHelper::generic(m_root)}, {"timeoutSeconds", 30}, {"savePassword", false}};
        m_sessionHome = home;
        m_sessionProfile = profile;
    }

    [[nodiscard]] bool sessionScopesGone() const {
        std::ifstream input(m_root / "session-scopes");
        std::string path;

        while (std::getline(input, path)) {
            if (std::filesystem::exists(std::filesystem::path(std::u8string(path.begin(), path.end())))) {
                return false;
            }
        }

        return true;
    }

    void connectSession() {
        prepareSession();
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        TemporaryDirectory data;
        HeadlessProduct product(data.path());
        product.setProcessLauncher(std::make_unique<platform::NativeProcesses>());
        ASSERT_TRUE(product.boot().hasValue());
        const std::string directory = platform::PathTextHelper::generic(Resources::staged() / "plugins" / "remote-ssh");
        const std::string setup = "local DIRECTORY = " + nlohmann::json(directory).dump() + "\nlocal EXECUTABLES = " + nlohmann::json(platform::PathTextHelper::generic(std::filesystem::path(WORKPANE_TEST_ASKPASS).parent_path())).dump() + "\nlocal PROFILE = require('json').decode(" + nlohmann::json(m_sessionProfile.dump()).dump() + ")\n";
        ASSERT_TRUE(product.product()
                        .runtime()
                        .loadString(setup + R"lua(
            local async = require("async")
            local bridge = require("workpane.bridge")
            local function record(value)
                bridge.call("workpane_log", { plugin = "remote-ssh", level = "info", category = "ssh-session-suite", message = "Session verification finished", details = value })
            end

            local info = bridge.call("workpane_app_info", {})
            info.paths.executables = EXECUTABLES
            local environment = setmetatable({}, { __index = _G })
            environment.workpane = require("workpane.api").create("remote-ssh", DIRECTORY, info)
            local loaded = {}

            function environment.include(name)
                if loaded[name] == nil then
                    loaded[name] = assert(loadfile(DIRECTORY .. "/" .. name .. ".lua", "t", environment))()
                end

                return loaded[name]
            end

            local stages = {}
            local connected
            connected = assert(environment.include("session").new(PROFILE, function() stages[#stages + 1] = connected.state end))
            environment.workpane.task(function()
                local outcome = connected:start():await()

                if outcome.failure ~= nil then
                    record({ failure = outcome.failure, stages = stages })
                    return
                end

                local root, problem = connected:request("files.stat", { path = "." }, 10000)
                local private = connected.scope.path
                local closed = connected:close():await()
                record({ root = root, failure = problem or closed.failure, state = connected.state, stages = stages, private = private, identity = connected.identity })
            end)
        )lua",
                                    "ssh-session")
                        .hasValue());
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&product]() { return !product.query("SELECT details_json FROM logs__entries WHERE category = 'ssh-session-suite'").empty(); }));
        // clang-format on

        const auto rows = product.query("SELECT details_json FROM logs__entries WHERE category = 'ssh-session-suite'");
        const auto result = nlohmann::json::parse(rows.front()["details_json"].get<std::string>());
        ASSERT_FALSE(result.contains("failure")) << result.dump();
        EXPECT_EQ(result["state"], "disconnected");
        const auto privateScope = result["private"].get<std::string>();
        EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(std::u8string(privateScope.begin(), privateScope.end()))));
        EXPECT_EQ(result["root"]["path"], platform::PathTextHelper::generic(m_root));
        EXPECT_EQ(result["identity"]["root"], platform::PathTextHelper::generic(m_root));
        EXPECT_EQ(result["stages"], nlohmann::json::array({"detecting", "connecting", "transferring", "connected", "closing", "disconnected"}));
        product.stop();

        EXPECT_TRUE(sessionScopesGone());
    }

    void prepareLanguageServer() const {
        std::filesystem::create_directory(m_root / "server-bin");
        const auto server = m_root / "server-bin" / "lua-language-server";
        std::ofstream(server) << R"python(#!/usr/bin/python3
import json
import pathlib
import signal
import sys

root = pathlib.Path.cwd()
running = root / ".server-running"
running.write_text("running")

def stop(signum, frame):
    raise SystemExit(0)

def send(message):
    body = json.dumps(message).encode("utf-8")
    sys.stdout.buffer.write(b"Content-Length: " + str(len(body)).encode("ascii") + b"\r\n\r\n" + body)
    sys.stdout.buffer.flush()

signal.signal(signal.SIGTERM, stop)

try:
    while True:
        line = sys.stdin.buffer.readline()
        if not line:
            break

        length = int(line.decode("ascii").split(":", 1)[1].strip())
        while sys.stdin.buffer.readline() != b"\r\n":
            pass

        message = json.loads(sys.stdin.buffer.read(length))
        method = message.get("method")

        if method == "initialize":
            (root / ".server-initialize.json").write_text(json.dumps(message["params"]))
            send({"jsonrpc": "2.0", "id": message["id"], "result": {"capabilities": {"textDocumentSync": {"openClose": True, "change": 1}, "completionProvider": {}}}})
        elif method == "textDocument/didOpen":
            send({"jsonrpc": "2.0", "method": "textDocument/publishDiagnostics", "params": {"uri": message["params"]["textDocument"]["uri"], "diagnostics": [{"range": {"start": {"line": 0, "character": 0}, "end": {"line": 0, "character": 5}}, "message": "Remote diagnostic", "severity": 2, "source": "Fixture"}]}})
        elif method == "shutdown":
            (root / ".server-shutdown").write_text("shutdown")
            send({"jsonrpc": "2.0", "id": message["id"], "result": None})
        elif method == "exit":
            (root / ".server-exit").write_text("exit")
            break
finally:
    running.unlink(missing_ok=True)
)python";
        std::filesystem::permissions(server, std::filesystem::perms::owner_all);
    }

    void verifyEditorReconnect(HeadlessProduct& product, std::string_view page, ui::NodeId grid, std::string_view editorPage, ui::NodeId oldEditor) {
        const auto document = m_root / "editor.txt";
        const SurfaceReader disconnected(product.declared(editorPage));
        const auto oldTree = disconnected.nodes("tree")[0];
        std::optional<ui::NodeId> workspaces;
        std::string oldWorkspace;

        for (const auto node : disconnected.nodes("tabs")) {
            const auto properties = disconnected.properties(node);

            if (properties["items"].size() == 1 && properties["items"][0].value("tooltip", "") == platform::PathTextHelper::generic(m_root)) {
                workspaces = node;
                oldWorkspace = properties["current"].get<std::string>();
            }
        }

        ASSERT_TRUE(workspaces.has_value());
        const auto originalTabs = disconnected.properties(*workspaces)["items"];
        EXPECT_EQ(originalTabs[0]["text"], "Development");
        EXPECT_EQ(originalTabs[0]["tooltip"], platform::PathTextHelper::generic(m_root));
        std::ofstream(document, std::ios::binary | std::ios::trunc) << "Reconnected contents\n";
        product.emit(page, grid, "action", {{"id", "development"}, {"action", "connect"}});
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&product, page]() {
            const SurfaceReader reader(product.declared(page));

            return reader.properties(reader.nodes("table")[0])["rows"][0]["cells"][4] == "Connected";
        }));
        // clang-format on

        product.emit(page, grid, "action", {{"id", "development"}, {"action", "editor"}});
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&product, &workspaces, &oldWorkspace, editorPage]() {
            const auto properties = SurfaceReader(product.declared(editorPage)).properties(*workspaces);

            return properties["items"].size() == 2 && properties["current"] != oldWorkspace;
        }));
        // clang-format on

        const auto currentTabs = SurfaceReader(product.declared(editorPage)).properties(*workspaces)["items"];

        for (const auto& item : currentTabs) {
            EXPECT_EQ(item["text"], "Development");
            EXPECT_EQ(item["tooltip"], platform::PathTextHelper::generic(m_root));
        }

        std::optional<ui::NodeId> newTree;
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&product, &newTree, &document, oldTree, editorPage]() {
            const SurfaceReader reader(product.declared(editorPage));

            for (const auto node : reader.nodes("tree")) {
                if (node == oldTree) {
                    continue;
                }

                const auto properties = reader.properties(node);

                for (const auto& item : properties["items"]) {
                    if (item["id"] == platform::PathTextHelper::generic(document)) {
                        newTree = node;
                        return true;
                    }
                }
            }

            return false;
        }));
        // clang-format on

        product.emit(editorPage, *newTree, "activate", {{"id", platform::PathTextHelper::generic(document)}});
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&product, oldEditor, editorPage]() {
            const SurfaceReader reader(product.declared(editorPage));

            for (const auto node : reader.nodes("codeEditor")) {
                if (node != oldEditor && reader.properties(node)["value"] == "Reconnected contents\n") {
                    return true;
                }
            }

            return false;
        }));
        // clang-format on

        EXPECT_EQ(SurfaceReader(product.declared(editorPage)).properties(oldEditor)["value"], "Edited Unsaved original\n");
        const auto script = R"lua(
            local events = require("workpane.events")
            require("workpane.task").run("code-editor", "suite", function()
                assert(#remoteEditorScopes == 2 and remoteEditorScopes[1] ~= remoteEditorScopes[2])
                local _, read = events.request("code-editor", "ssh.workspace.request", { workspace = remoteEditorScopes[1], method = "files.read", parameters = { path = "editor.txt" } }):await()
                local _, start = events.request("code-editor", "ssh.workspace.request", { workspace = remoteEditorScopes[1], method = "process.start", parameters = { program = "/bin/sh", arguments = { "-c", "touch stale-start" }, directory = ROOT } }):await()
                assert(read.code == "ssh_workspace_unavailable" or read.code == "ssh_connection_closed")
                assert(start.code == "ssh_workspace_unavailable" or start.code == "ssh_connection_closed")
                local _, link = events.request("ai", "workspace.remote-path.open", { profile = "development", session = "previous-session", path = ROOT .. "/editor.txt", line = 1, column = 2 }):await()
                assert(link.code == "ssh_connection_closed" or link.code == "ssh_workspace_unavailable")
                require("workpane.bridge").call("workpane_log", { plugin = "code-editor", level = "info", category = "suite", message = "remote-editor-stale-scope-refused", details = {} })
            end)
        )lua";
        ASSERT_TRUE(product.product().runtime().loadString("local ROOT = " + nlohmann::json(platform::PathTextHelper::generic(m_root)).dump() + "\n" + script, "remote-editor-stale-scope").hasValue());
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&product]() { return !product.query("SELECT sequence FROM logs__entries WHERE message = 'remote-editor-stale-scope-refused'").empty(); }));
        // clang-format on

        EXPECT_FALSE(std::filesystem::exists(m_root / "stale-start"));
        product.emit(page, grid, "action", {{"id", "development"}, {"action", "disconnect"}});
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&product, page]() {
            const SurfaceReader reader(product.declared(page));

            return reader.properties(reader.nodes("table")[0])["rows"][0]["cells"][4] == "Disconnected";
        }));
        // clang-format on
    }

    [[nodiscard]] httplib::Result forwardedRequest(HeadlessProduct& product, int port) {
        // clang-format off
        auto request = std::async(std::launch::async, [port]() {
            httplib::Client client("127.0.0.1", port);
            client.set_connection_timeout(2);
            client.set_read_timeout(2);

            return client.Get("/forwarded");
        });
        // clang-format on

        // clang-format off
        const bool completed = product.frameUntil([&request]() { return request.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready; }, std::chrono::seconds(5));
        // clang-format on
        EXPECT_TRUE(completed);

        return request.get();
    }

    void verifyPorts(HeadlessProduct& product, std::string_view page, ui::NodeId manager, int& port) {
        product.emit(page, manager, "action", {{"id", "development"}, {"action", "ports"}});
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&product, page]() { return SurfaceReader(product.declared(page)).nodeShowing("remote-ssh.ports.add").has_value(); }));
        // clang-format on
        const SurfaceReader controls(product.declared(page));
        const auto fields = controls.nodes("numberField");
        ASSERT_EQ(fields.size(), 2U);
        const auto add = controls.nodeShowing("remote-ssh.ports.add");
        ASSERT_TRUE(add.has_value());
        product.emit(page, fields[0], "change", {{"value", m_forwardPort}});
        product.emit(page, fields[1], "change", {{"value", 0}});
        product.emit(page, *add, "click", nlohmann::json::object());
        std::optional<ui::NodeId> table;
        std::string id;
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&product, page, &table, &port, &id, manager]() {
            const SurfaceReader reader(product.declared(page));

            for (const auto node : reader.nodes("table")) {
                if (node == manager) {
                    continue;
                }

                const auto properties = reader.properties(node);

                if (!properties["rows"].empty()) {
                    table = node;
                    id = properties["rows"][0]["id"].get<std::string>();
                    const auto endpoint = properties["rows"][0]["cells"][1].get<std::string>();
                    port = std::stoi(endpoint.substr(endpoint.find(':') + 1));

                    return port > 0;
                }
            }

            return false;
        }));
        // clang-format on

        auto response = forwardedRequest(product, port);
        ASSERT_TRUE(response);
        EXPECT_EQ(response->status, 200);
        EXPECT_EQ(response->body, std::string("\0\xff\x80", 3) + std::string(100000, 'x'));
        product.emit(page, *table, "action", {{"id", id}, {"action", "browser"}});
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&product, &port]() { return !product.webViews().navigations.empty() && product.webViews().navigations.back() == "http://127.0.0.1:" + std::to_string(port) + "/"; }));
        // clang-format on
        ASSERT_TRUE(product.navigate("remote-ssh:connections").hasValue());
        response = forwardedRequest(product, port);
        ASSERT_TRUE(response);
        EXPECT_EQ(response->status, 200);
        product.emit(page, *table, "action", {{"id", id}, {"action", "close"}});
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&product, page, table]() { return SurfaceReader(product.declared(page)).properties(*table)["rows"].empty(); }));
        // clang-format on
        EXPECT_FALSE(forwardedRequest(product, port));
        product.emit(page, fields[1], "change", {{"value", port}});
        product.emit(page, *add, "click", nlohmann::json::object());
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&product, page, table]() { return !SurfaceReader(product.declared(page)).properties(*table)["rows"].empty(); }));
        // clang-format on
        EXPECT_TRUE(forwardedRequest(product, port));
    }

    void setConsumerEnabled(HeadlessProduct& product, std::string_view owner, std::string_view capability, bool enabled) {
        ASSERT_TRUE(product.navigate(ui::Shell::settingsDestination).hasValue());
        product.frame();
        product.frame();
        product.press(HeadlessProduct::command() | ImGuiKey_F);
        product.frame();
        product.press(HeadlessProduct::command() | ImGuiKey_A);
        product.type("plugin");
        constexpr std::string_view installed{"settings:workpane:plugins:installed"};
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&product, installed]() { return SurfaceReader(product.declared(installed)).mounted(); }));
        // clang-format on
        const SurfaceReader plugins(product.declared(installed));
        const auto rows = plugins.nodes("settingsRow");
        const auto toggles = plugins.nodes("toggle");
        std::optional<ui::NodeId> toggle;

        for (std::size_t index = 0; index < rows.size() && index < toggles.size(); ++index) {
            if (plugins.properties(rows[index])["label"].value("key", "") == std::string(owner) + ".plugin.title") {
                toggle = toggles[index];
            }
        }

        ASSERT_TRUE(toggle.has_value());
        product.emit(installed, *toggle, "change", {{"checked", enabled}});
        const std::string marker = enabled ? "native-consumer-enabled" : "native-consumer-disabled";
        const std::string script = "local EXPECTED = " + nlohmann::json(enabled).dump() + "\nlocal MARKER = " + nlohmann::json(marker).dump() + "\nlocal CAPABILITY = " + nlohmann::json(capability).dump() + "\n" + R"lua(
            local task = require("workpane.task")
            task.run("suite", "suite", function()
                while require("workpane.events").available(CAPABILITY) ~= EXPECTED do
                    require("async").sleep(1):await()
                end
                require("workpane.bridge").call("workpane_log", { plugin = "logs", level = "info", category = "suite", message = MARKER, details = {} })
            end)
        )lua";
        ASSERT_TRUE(product.product().runtime().loadString(script, "native-consumer-switch").hasValue());
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&product, &marker]() { return !product.query("SELECT message FROM logs__entries WHERE message = ?", {marker}).empty(); }));
        // clang-format on
    }

    static void taskAction(HeadlessProduct& product, std::string_view key) {
        constexpr std::string_view board{"view:ai:tasks"};
        std::optional<ui::NodeId> button;
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&product, key, &button, board]() {
            const SurfaceReader reader(product.declared(board));

            for (const auto candidate : reader.nodes("button")) {
                const auto properties = reader.properties(candidate);

                if (properties.contains("tooltip") && properties["tooltip"].value("key", "") == key && properties.value("enabled", true)) {
                    button = candidate;
                    return true;
                }
            }

            return false;
        }));
        // clang-format on

        product.emit(board, *button, "click", nlohmann::json::object());
    }

    void selectRemoteFolder(HeadlessProduct& product, const std::string& form, std::string_view owner, std::size_t fieldIndex) {
        const auto directory = m_root / "picker-directory";
        std::filesystem::create_directory(directory);
        const std::string root = platform::PathTextHelper::generic(m_root);
        const auto field = SurfaceReader(product.declared(form)).nodes("pathField")[fieldIndex];
        product.recordCalls("workpane_native_folder");
        product.emit(form, field, "browse", {{"value", root}, {"mode", "folder"}, {"title", "Remote folder"}, {"filters", nlohmann::json::array()}, {"picking", true}}, {{"picking", "picking"}});
        std::string picker;
        std::optional<ui::NodeId> entries;
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&product, &picker, &entries, owner, &form]() {
            for (int number = 1; number < 64; ++number) {
                const std::string candidate = "dialog:" + std::string(owner) + ":" + std::to_string(number);
                const SurfaceReader reader(product.declared(candidate));
                const auto lists = reader.nodes("list");

                if (candidate != form && reader.mounted() && lists.size() == 1U) {
                    const auto properties = reader.properties(lists[0]);

                    for (const auto& item : properties["items"]) {
                        if (item["text"] == "picker-directory" && properties.value("enabled", true)) {
                            picker = candidate;
                            entries = lists[0];
                            return true;
                        }
                    }
                }
            }

            return false;
        }));
        // clang-format on

        product.emit(picker, *entries, "activate", {{"id", platform::PathTextHelper::generic(directory)}});
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&product, &picker, &directory, &entries]() {
            const SurfaceReader reader(product.declared(picker));
            return reader.properties(reader.nodes("textField")[0])["value"] == platform::PathTextHelper::generic(directory) && reader.properties(*entries).value("enabled", true);
        }));
        // clang-format on

        product.emit(picker, *SurfaceReader(product.declared(picker)).nodeShowing("workpane.paths.parent"), "click", nlohmann::json::object());
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&product, &picker, &root, &entries]() {
            const SurfaceReader reader(product.declared(picker));
            return reader.properties(reader.nodes("textField")[0])["value"] == root && reader.properties(*entries).value("enabled", true);
        }));
        // clang-format on

        ASSERT_TRUE(product.answerDialog("select"));
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&product, &picker, &form, field]() { return !SurfaceReader(product.declared(picker)).mounted() && !SurfaceReader(product.declared(form)).properties(field).value("picking", false); }));
        // clang-format on

        EXPECT_EQ(SurfaceReader(product.declared(form)).properties(field)["value"], root);
        EXPECT_EQ(product.calls("workpane_native_folder"), 0U);
    }

    void verifyRemoteTask(HeadlessProduct& product, ManagerScenario scenario) {
        constexpr std::string_view board{"view:ai:tasks"};
        ASSERT_TRUE(product.navigate("ai:tasks").hasValue());
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&product, board]() { return SurfaceReader(product.declared(board)).nodeShowing("ai.workspace.add").has_value(); }));
        // clang-format on
        product.emit(board, *SurfaceReader(product.declared(board)).nodeShowing("ai.workspace.add"), "click", nlohmann::json::object());
        // clang-format off
        ASSERT_TRUE(product.frameUntil([]() { return ImGui::GetTopMostPopupModal() != nullptr; }));
        // clang-format on
        product.frame();
        product.press(HeadlessProduct::command() | ImGuiKey_A);
        product.type("Remote tasks");
        ASSERT_TRUE(product.answerDialog("confirm"));
        taskAction(product, "ai.task.add");
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        std::string form;
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&product, &form]() {
            for (int number = 1; number < 32; ++number) {
                const std::string candidate = "dialog:ai:" + std::to_string(number);

                if (SurfaceReader(product.declared(candidate)).mounted() && SurfaceReader(product.declared(candidate)).showsKey("ai.task.execution-kind")) {
                    form = candidate;
                    return true;
                }
            }

            return false;
        }));
        // clang-format on

        const SurfaceReader formReader(product.declared(form));
        const auto combos = formReader.nodes("combo");
        const auto fields = formReader.nodes("textField");
        ASSERT_EQ(combos.size(), 4U);
        ASSERT_GE(fields.size(), 5U);
        const bool agent = scenario == ManagerScenario::TaskAgent || scenario == ManagerScenario::TaskCli;
        product.emit(form, combos[0], "change", {{"value", agent ? "agent" : "command"}});
        product.emit(form, combos[2], "change", {{"value", "ssh:development"}});

        if (scenario == ManagerScenario::Task) {
            product.frame();
            selectRemoteFolder(product, form, "ai", 0);
            ASSERT_FALSE(::testing::Test::HasFatalFailure());
        }

        product.emit(form, fields[0], "change", {{"value", "Remote command"}});
        const auto command = scenario == ManagerScenario::Task ? "printf 'remote-task|'; pwd" : "printf 'remote-task-started'; : > task-started; sleep 30";

        if (agent) {
            const auto prompts = formReader.nodes("textArea");
            ASSERT_EQ(prompts.size(), 1U);
            product.emit(form, prompts[0], "change", {{"value", "Update the remote file and execute a command."}});
        } else {
            product.emit(form, fields[3], "change", {{"value", command}});
        }

        product.frame();
        ASSERT_TRUE(product.answerDialog("save"));
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&product]() { return !product.query("SELECT id FROM ai__tasks WHERE title = 'Remote command'").empty(); }));
        // clang-format on
        const auto saved = product.query("SELECT host_kind, host_profile, host_platform, workdir FROM ai__tasks WHERE title = 'Remote command'")[0];
        EXPECT_EQ(saved["host_kind"], "ssh");
        EXPECT_EQ(saved["host_profile"], "development");
        EXPECT_EQ(saved["host_platform"], std::string(nativePlatform));
        EXPECT_EQ(saved["workdir"], platform::PathTextHelper::generic(m_root));
        taskAction(product, "ai.task.start");
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        const auto expected = scenario == ManagerScenario::Task || agent ? "succeeded" : "running";
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&product, expected]() { const auto rows = product.query("SELECT status FROM ai__executions ORDER BY started_at_utc DESC LIMIT 1"); return !rows.empty() && rows[0]["status"] == expected; }));
        // clang-format on

        if (scenario == ManagerScenario::Task) {
            const auto content = product.query("SELECT content FROM ai__executions")[0]["content"].get<std::string>();
            EXPECT_EQ(content, "remote-task|" + platform::PathTextHelper::generic(m_root) + "\n");
        }

        if (scenario != ManagerScenario::Task && !agent) {
            // clang-format off
            ASSERT_TRUE(product.frameUntil([this]() { return std::filesystem::exists(m_root / "task-started"); }));
            // clang-format on
        }

        if (scenario == ManagerScenario::TaskCancel) {
            taskAction(product, "ai.task.stop");
            ASSERT_FALSE(::testing::Test::HasFatalFailure());
            // clang-format off
            ASSERT_TRUE(product.frameUntil([&product]() { return product.query("SELECT status FROM ai__executions")[0]["status"] == "cancelled"; }));
            // clang-format on
        }

        ASSERT_TRUE(product.navigate("remote-ssh:connections").hasValue());
    }

    void connectManager(ManagerScenario scenario) {
        prepareSession();
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        const bool trust = scenario == ManagerScenario::Trust || scenario == ManagerScenario::DeclinedTrust;
        const bool terminalScenario = scenario == ManagerScenario::Terminal || scenario == ManagerScenario::TerminalBash || scenario == ManagerScenario::TerminalLost || scenario == ManagerScenario::TerminalStop;
        const bool languageScenario = scenario == ManagerScenario::EditorLanguage;
        const bool editorScenario = scenario == ManagerScenario::Editor || scenario == ManagerScenario::EditorConflict || scenario == ManagerScenario::EditorReconnect;
        std::unique_ptr<FakeProvider> provider;

        if (scenario == ManagerScenario::TaskAgent) {
            provider = std::make_unique<FakeProvider>();
            std::filesystem::create_directory(m_root / ".git");
            std::ofstream(m_root / "AGENTS.md") << "Remote repository instructions.";
            provider->script(FakeProvider::calls(nlohmann::json::array({{{"id", "remote-write"}, {"name", "write_file"}, {"arguments", {{"path", "agent.txt"}, {"content", "Initial remote text\n"}}}}})));
            provider->script(FakeProvider::calls(nlohmann::json::array({{{"id", "remote-edit"}, {"name", "edit_file"}, {"arguments", {{"path", "agent.txt"}, {"old_text", "Initial"}, {"new_text", "Final"}}}}})));
            provider->script(FakeProvider::calls(nlohmann::json::array({{{"id", "remote-read"}, {"name", "read_file"}, {"arguments", {{"path", "agent.txt"}}}}, {{"id", "remote-command"}, {"name", "run_command"}, {"arguments", {{"command", "printf 'remote-agent|'; pwd"}}}}})));
            const auto image = text::Base64Helper::decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8DwHwAFBQIAX8jx0gAAAABJRU5ErkJggg==");
            ASSERT_TRUE(image.has_value());
            std::ofstream picture(m_root / "image.png", std::ios::binary);

            for (const auto byte : *image) {
                picture.put(static_cast<char>(byte));
            }

            provider->script(FakeProvider::answer("Remote agent complete. [Source](agent.txt:1:2)\n\n![Preview](image.png)"));
        }

        if (scenario == ManagerScenario::TaskCli) {
            std::filesystem::create_directory(m_root / "server-bin");
            const auto program = m_root / "server-bin" / "codex";
            std::ofstream(program) << "#!/bin/sh\nprintf '%s\\n' \"$@\" > cli-arguments\ncat > cli-prompt\nprintf 'Remote CLI complete|'; pwd\nprintf 'Private remote diagnostic' >&2\n";
            std::filesystem::permissions(program, std::filesystem::perms::owner_all);
        }

        if (languageScenario) {
            prepareLanguageServer();
        }

        if (trust) {
            std::ofstream(m_root / "known_hosts", std::ios::trunc);
        }

        if (scenario == ManagerScenario::Password || scenario == ManagerScenario::ProfileFolderCredentialsCancel) {
            const auto keygen = m_launcher.find("ssh-keygen", {"/usr/bin"});
            ASSERT_TRUE(keygen.has_value());
            const auto output = std::make_shared<Output>();
            auto protectedKey = start(*keygen, {"-p", "-P", "", "-N", "Private fixture passphrase", "-f", path("client")}, "", output);
            ASSERT_NE(protectedKey, nullptr);
            std::unique_lock lock(output->mutex);
            // clang-format off
            ASSERT_TRUE(output->changed.wait_for(lock, std::chrono::seconds(10), [&output]() { return output->end.has_value(); }));
            // clang-format on
            ASSERT_EQ(output->end->code, 0);
        }

        const auto resources = m_root / "resources";
        std::filesystem::create_directory(resources);

        for (const auto& entry : std::filesystem::directory_iterator(Resources::staged())) {
            if (entry.path().filename() != "plugins") {
                std::filesystem::create_symlink(entry.path(), resources / entry.path().filename());
            }
        }

        std::filesystem::create_directory(resources / "plugins");

        for (const auto& entry : std::filesystem::directory_iterator(Resources::staged() / "plugins")) {
            if (entry.path().filename() == "remote-ssh") {
                std::filesystem::copy(entry.path(), resources / "plugins" / "remote-ssh", std::filesystem::copy_options::recursive);
            } else {
                std::filesystem::create_directory_symlink(entry.path(), resources / "plugins" / entry.path().filename());
            }
        }

        const auto sourceFile = resources / "plugins" / "remote-ssh" / "source.lua";
        std::ifstream sourceInput(sourceFile);
        std::string source{std::istreambuf_iterator<char>(sourceInput), std::istreambuf_iterator<char>()};
        sourceInput.close();
        const auto environment = "HOME=" + quoted(platform::PathTextHelper::generic(m_sessionHome)) + " " + (languageScenario || scenario == ManagerScenario::TaskCli ? "PATH=" + quoted(platform::PathTextHelper::generic(m_root / "server-bin") + ":/usr/bin:/bin:/usr/sbin:/sbin") + " " : "");
        source.insert(source.rfind("return source"), "local original = source.prepare\nsource.prepare = function(...)\n    local value = original(...)\n    value.command = " + nlohmann::json(environment).dump() + " .. value.command\n\n    return value\nend\n\n");
        std::ofstream(sourceFile) << source;
        const auto scopeFile = resources / "plugins" / "remote-ssh" / "assets" / "ssh-scope-posix.sh";
        std::ifstream scopeInput(scopeFile);
        std::string scope{std::istreambuf_iterator<char>(scopeInput), std::istreambuf_iterator<char>()};
        scopeInput.close();
        scope.insert(scope.find("printf 'SCOPE "), "printf '%s\\n' \"$scope\" >> " + quoted(path("session-scopes")) + "\n");
        scope.insert(scope.find("encodedHome=$(printf"), "accountHome=" + quoted(platform::PathTextHelper::generic(m_sessionHome)) + "\n            ");

        if (scenario == ManagerScenario::TerminalBash) {
            scope.insert(scope.find("encodedHome=$(printf"), "shell=/bin/bash\n            ");
        }

        std::ofstream(scopeFile) << scope;
        TemporaryDirectory data;

        {
            auto database = ProductDatabase::open(data);
            ASSERT_TRUE(persistence::PreferenceStore::store(database, "remote-ssh", {{"profiles", nlohmann::json::array({m_sessionProfile})}}).hasValue());

            if (provider != nullptr) {
                const nlohmann::json connection{{"providerId", "ollama"}, {"modelId", "fake-large"}, {"displayName", "Fixture"}, {"apiKey", ""}, {"address", provider->address()}, {"parameters", {{"maxOutputTokens", 1024}, {"temperature", 0.5}, {"topP", 0.95}, {"frequencyPenalty", 0.0}, {"presencePenalty", 0.0}}}, {"extraParameters", ""}};
                const nlohmann::json agent{{"id", "remote-builder"}, {"name", "Remote builder"}, {"description", ""}, {"systemPrompt", "{{SYSTEM_PROMPT_DATA}}"}, {"connectionKey", "ollama/fake-large"}, {"maximumIterations", 8}};
                ASSERT_TRUE(persistence::PreferenceStore::store(database, "ai", {{"connections", nlohmann::json::array({connection})}, {"defaultConnectionKey", "ollama/fake-large"}, {"agents", nlohmann::json::array({agent})}}).hasValue());
            }

            if (scenario == ManagerScenario::TaskCli) {
                const nlohmann::json connection{{"providerId", "codex-cli"}, {"modelId", "gpt-5.4"}, {"displayName", ""}, {"apiKey", ""}, {"address", ""}, {"parameters", nlohmann::json::object()}, {"extraParameters", ""}};
                const nlohmann::json agent{{"id", "remote-cli"}, {"name", "Remote CLI"}, {"description", ""}, {"systemPrompt", "{{SYSTEM_PROMPT_DATA}}"}, {"connectionKey", "codex-cli/gpt-5.4"}, {"maximumIterations", 8}};
                ASSERT_TRUE(persistence::PreferenceStore::store(database, "ai", {{"connections", nlohmann::json::array({connection})}, {"agents", nlohmann::json::array({agent})}}).hasValue());
            }
        }

        auto productOwner = std::make_unique<HeadlessProduct>(data.path(), "en_US", resources);
        auto& product = *productOwner;
        product.setProcessLauncher(std::make_unique<platform::NativeProcesses>());

        if (terminalScenario) {
            product.setTerminalHost(std::make_unique<platform::NativePseudoTerminals>());
        }

        ASSERT_TRUE(product.boot().hasValue());
        ASSERT_TRUE(product.navigate("remote-ssh:connections").hasValue());
        constexpr std::string_view page{"view:remote-ssh:connections"};
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&product, page]() { return SurfaceReader(product.declared(page)).mounted(); }));
        // clang-format on

        const auto grid = SurfaceReader(product.declared(page)).nodes("table")[0];
        product.recordCalls("workpane_process_write");

        if (scenario == ManagerScenario::ProfileFolder || scenario == ManagerScenario::ProfileFolderCancel || scenario == ManagerScenario::ProfileFolderCredentialsCancel) {
            product.emit(page, grid, "action", {{"id", "development"}, {"action", "edit"}});
            std::string form;
            // clang-format off
            ASSERT_TRUE(product.frameUntil([&product, &form]() {
                for (int number = 1; number < 32; ++number) {
                    const auto candidate = "dialog:remote-ssh:" + std::to_string(number);

                    if (SurfaceReader(product.declared(candidate)).nodes("pathField").size() == 3U) {
                        form = candidate;
                        return true;
                    }
                }

                return false;
            }));
            // clang-format on

            if (scenario == ManagerScenario::ProfileFolder) {
                selectRemoteFolder(product, form, "remote-ssh", 2);
                ASSERT_FALSE(::testing::Test::HasFatalFailure());
            } else {
                const auto field = SurfaceReader(product.declared(form)).nodes("pathField")[2];
                product.emit(form, field, "browse", {{"value", path("")}, {"mode", "folder"}, {"title", "Remote folder"}, {"filters", nlohmann::json::array()}, {"picking", true}}, {{"picking", "picking"}});
                product.frame();

                if (scenario == ManagerScenario::ProfileFolderCancel) {
                    // clang-format off
                    ASSERT_TRUE(product.frameUntil([this]() { return std::filesystem::exists(m_root / "session-scopes"); }));
                    // clang-format on
                }

                if (scenario == ManagerScenario::ProfileFolderCredentialsCancel) {
                    std::string prompt;
                    // clang-format off
                    ASSERT_TRUE(product.frameUntil([&product, &prompt, &form]() {
                        for (int number = 1; number < 32; ++number) {
                            const auto candidate = "dialog:remote-ssh:" + std::to_string(number);
                            const SurfaceReader reader(product.declared(candidate));
                            const auto fields = reader.nodes("textField");

                            if (candidate != form && fields.size() == 1U && reader.properties(fields[0]).value("password", false)) {
                                prompt = candidate;
                                return true;
                            }
                        }

                        return false;
                    }));
                    // clang-format on

                    ASSERT_TRUE(product.answerDialog("cancel"));
                    // clang-format off
                    ASSERT_TRUE(product.frameUntil([&product, &prompt, &form, field]() { return !SurfaceReader(product.declared(prompt)).mounted() && !SurfaceReader(product.declared(form)).properties(field).value("picking", false); }));
                    // clang-format on
                }
            }

            ASSERT_TRUE(product.answerDialog("cancel"));
            // clang-format off
            ASSERT_TRUE(product.frameUntil([this, &product, &form]() { return !SurfaceReader(product.declared(form)).mounted() && !product.product().shell().dialogs().active() && sessionScopesGone(); }));
            // clang-format on
            EXPECT_EQ(SurfaceReader(product.declared(page)).properties(grid)["rows"][0]["cells"][4], "Disconnected");
            product.stop();
            EXPECT_TRUE(sessionScopesGone());
            EXPECT_TRUE(product.query("SELECT message FROM logs__entries WHERE level = 'error'").empty());
            return;
        }

        product.emit(page, grid, "action", {{"id", "development"}, {"action", "connect"}});

        if (trust || scenario == ManagerScenario::Password) {
            const int prompts = 1;

            for (int index = 0; index < prompts; ++index) {
                std::string dialog;
                // clang-format off
                ASSERT_TRUE(product.frameUntil([&product, &dialog]() {
                    for (int number = 1; number < 16; ++number) {
                        const std::string candidate = "dialog:remote-ssh:" + std::to_string(number);

                        if (SurfaceReader(product.declared(candidate)).mounted()) {
                            dialog = candidate;
                            return true;
                        }
                    }

                    return false;
                }));
                // clang-format on

                const SurfaceReader reader(product.declared(dialog));
                const auto fields = reader.nodes("textField");

                if (scenario == ManagerScenario::Password) {
                    ASSERT_EQ(fields.size(), 1U);
                    EXPECT_EQ(reader.properties(fields[0])["password"], true);
                    // clang-format off
                    ASSERT_TRUE(product.frameUntil([]() {
                        const auto* editing = ImGui::GetInputTextState(ImGui::GetActiveID());
                        return editing != nullptr && (editing->Flags & ImGuiInputTextFlags_Password) != 0;
                    }));
                    // clang-format on

                    product.type("Private fixture passphrase");
                } else {
                    EXPECT_TRUE(fields.empty());
                }

                if (scenario == ManagerScenario::Password || scenario == ManagerScenario::Trust) {
                    product.press(ImGuiKey_Enter);
                } else {
                    ASSERT_TRUE(product.answerDialog(scenario == ManagerScenario::DeclinedTrust ? "cancel" : "accept"));
                }

                // clang-format off
                ASSERT_TRUE(product.frameUntil([&product, &dialog]() { return !SurfaceReader(product.declared(dialog)).mounted(); }));
                // clang-format on
            }
        }

        if (scenario == ManagerScenario::DeclinedTrust) {
            // clang-format off
            ASSERT_TRUE(product.frameUntil([&product, page]() {
                const SurfaceReader reader(product.declared(page));

                return reader.properties(reader.nodes("table")[0])["rows"][0]["cells"][4] == "Disconnected";
            }));
            // clang-format on

            EXPECT_TRUE(sessionScopesGone());
            EXPECT_TRUE(product.query("SELECT details_json FROM logs__entries WHERE source = 'remote-ssh' AND level = 'error'").empty());
            product.stop();
            return;
        }

        // clang-format off
        ASSERT_TRUE(product.frameUntil([&product, page]() {
            const SurfaceReader reader(product.declared(page));

            return reader.properties(reader.nodes("table")[0])["rows"][0]["cells"][4] == "Connected";
        }));
        // clang-format on

        const auto connectionProcess = product.lastCall("workpane_process_write")["process"];
        ASSERT_TRUE(connectionProcess.is_number_integer());
        constexpr std::string_view editorPage{"view:code-editor:editor"};
        std::optional<ui::NodeId> remoteEditor;
        int forwardedPort = 0;
        const bool portsScenario = scenario == ManagerScenario::Ports || scenario == ManagerScenario::PortsLost || scenario == ManagerScenario::PortsStop || scenario == ManagerScenario::PortsReconnect;
        std::string previousForward;
        const bool taskScenario = scenario == ManagerScenario::Task || scenario == ManagerScenario::TaskCancel || scenario == ManagerScenario::TaskLost || scenario == ManagerScenario::TaskStop || scenario == ManagerScenario::TaskAgent || scenario == ManagerScenario::TaskCli;

        if (taskScenario) {
            verifyRemoteTask(product, scenario);
            ASSERT_FALSE(::testing::Test::HasFatalFailure());
        }

        if (provider != nullptr) {
            std::ifstream file(m_root / "agent.txt");
            EXPECT_EQ(std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()), "Final remote text\n");
            const auto requests = provider->requests();
            ASSERT_EQ(requests.size(), 4U);
            EXPECT_NE(requests.front().dump().find("Remote repository instructions."), std::string::npos);
            EXPECT_NE(requests.front().dump().find("- operating system: " + std::string(nativePlatform)), std::string::npos);
            EXPECT_NE(requests.back().dump().find("Final remote text"), std::string::npos);
            EXPECT_NE(requests.back().dump().find("remote-agent|" + platform::PathTextHelper::generic(m_root)), std::string::npos);
            ASSERT_TRUE(product.navigate("ai:tasks").hasValue());
            product.recordCalls("workpane_ui_command");
            taskAction(product, "ai.task.chat");
            ASSERT_FALSE(::testing::Test::HasFatalFailure());
            std::string conversation;
            std::optional<ui::NodeId> markdown;
            // clang-format off
            ASSERT_TRUE(product.frameUntil([&product, &conversation, &markdown]() {
                for (int number = 1; number < 32; ++number) {
                    const std::string candidate = "dialog:ai:" + std::to_string(number);
                    const SurfaceReader reader(product.declared(candidate));

                    for (const auto node : reader.nodes("markdown")) {
                        const auto content = reader.properties(node).value("text", "");

                        if (content.starts_with("Remote agent complete.") && reader.properties(node).value("pathLinks", false)) {
                            conversation = candidate;
                            markdown = node;
                            return true;
                        }
                    }
                }

                return false;
            }));
            // clang-format on

            // clang-format off
            ASSERT_TRUE(product.frameUntil([&product]() { const auto command = product.lastCall("workpane_ui_command"); return command.is_object() && command.value("command", "") == "load-image" && command["arguments"].value("source", "") == "image.png"; }));
            // clang-format on
            const auto imageReply = product.lastCall("workpane_ui_command")["arguments"];
            EXPECT_FALSE(imageReply.value("failed", false));
            EXPECT_EQ(imageReply["data"], "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8DwHwAFBQIAX8jx0gAAAABJRU5ErkJggg==");
            EXPECT_FALSE(imageReply.contains("file"));
            product.emit(conversation, *markdown, "link", {{"url", "agent.txt:1:2"}});
            // clang-format off
            ASSERT_TRUE(product.frameUntil([&product, editorPage]() {
                const SurfaceReader reader(product.declared(editorPage));

                for (const auto node : reader.nodes("codeEditor")) {
                    if (reader.properties(node).value("value", "") == "Final remote text\n") {
                        return !product.product().shell().dialogs().active();
                    }
                }

                return false;
            }));
            // clang-format on

            EXPECT_TRUE(product.query("SELECT path FROM code_editor__documents").empty());

            ASSERT_TRUE(product.navigate("remote-ssh:connections").hasValue());
        }

        if (scenario == ManagerScenario::TaskCli) {
            std::ifstream promptFile(m_root / "cli-prompt");
            const std::string prompt{std::istreambuf_iterator<char>(promptFile), std::istreambuf_iterator<char>()};
            EXPECT_NE(prompt.find("Update the remote file and execute a command."), std::string::npos);
            EXPECT_NE(prompt.find("- operating system: " + std::string(nativePlatform)), std::string::npos);
            std::ifstream argumentFile(m_root / "cli-arguments");
            const std::string arguments{std::istreambuf_iterator<char>(argumentFile), std::istreambuf_iterator<char>()};
            EXPECT_NE(arguments.find("gpt-5.4\n"), std::string::npos);
            EXPECT_NE(arguments.find(platform::PathTextHelper::generic(m_root) + "\n-\n"), std::string::npos);
            const auto output = product.query("SELECT content FROM ai__executions")[0]["content"].get<std::string>();
            EXPECT_EQ(output, "Remote CLI complete|" + platform::PathTextHelper::generic(m_root) + "\n");
            EXPECT_EQ(product.query("SELECT content FROM ai__messages WHERE role = 'assistant'")[0]["content"], "Remote CLI complete|" + platform::PathTextHelper::generic(m_root));
            EXPECT_EQ(output.find("Private remote diagnostic"), std::string::npos);
        }

        if (portsScenario) {
            verifyPorts(product, page, grid, forwardedPort);
            ASSERT_FALSE(::testing::Test::HasFatalFailure());

            if (scenario == ManagerScenario::PortsReconnect) {
                const SurfaceReader reader(product.declared(page));
                previousForward = reader.properties(reader.nodes("table")[1])["rows"][0]["id"].get<std::string>();
            }
        }

        if (scenario == ManagerScenario::EditorReconnect) {
            ASSERT_TRUE(product.product()
                            .runtime()
                            .loadString(R"lua(
                remoteEditorScopes = {}
                require("workpane.events").subscribe("code-editor", "remote-ssh.workspace.changed", function(value)
                    if value.connected then
                        remoteEditorScopes[#remoteEditorScopes + 1] = value.workspace
                    end
                end)
            )lua",
                                        "remote-editor-scope-observer")
                            .hasValue());
        }

        if (editorScenario || languageScenario) {
            const auto document = m_root / (languageScenario ? "editor.lua" : "editor.txt");
            std::ofstream(document) << "original\n";
            product.emit(page, grid, "action", {{"id", "development"}, {"action", "editor"}});
            // clang-format off
            ASSERT_TRUE(product.frameUntil([&product, editorPage]() {
                const SurfaceReader reader(product.declared(editorPage));

                return reader.mounted() && !reader.nodes("tree").empty() && !reader.properties(reader.nodes("tree")[0])["items"].empty();
            }));
            // clang-format on

            const SurfaceReader editorView(product.declared(editorPage));
            product.emit(editorPage, editorView.nodes("tree")[0], "activate", {{"id", platform::PathTextHelper::generic(document)}});
            // clang-format off
            ASSERT_TRUE(product.frameUntil([&product, &remoteEditor, editorPage]() {
                const SurfaceReader reader(product.declared(editorPage));

                for (const auto node : reader.nodes("codeEditor")) {
                    if (reader.properties(node)["value"] == "original\n") {
                        remoteEditor = node;
                        return true;
                    }
                }

                return false;
            }));
            // clang-format on

            if (languageScenario) {
                // clang-format off
                ASSERT_TRUE(product.frameUntil([&product, &remoteEditor, editorPage]() {
                    const auto properties = SurfaceReader(product.declared(editorPage)).properties(*remoteEditor);

                    return properties.value("completion", false) && !properties["markers"].empty() && properties["markers"][0]["message"] == "Remote diagnostic";
                }));
                // clang-format on

                std::ifstream initialized(m_root / ".server-initialize.json");
                const auto parameters = nlohmann::json::parse(initialized);
                EXPECT_TRUE(parameters["processId"].is_null());
                EXPECT_EQ(parameters["rootUri"], "file://" + platform::PathTextHelper::generic(m_root));
                EXPECT_TRUE(std::filesystem::exists(m_root / ".server-running"));
            }

            product.frame();
            product.type("Edited ");
            product.press(HeadlessProduct::command() | ImGuiKey_S);
            // clang-format off
            ASSERT_TRUE(product.frameUntil([&document]() {
                std::ifstream saved(document);
                const std::string bytes{std::istreambuf_iterator<char>(saved), std::istreambuf_iterator<char>()};

                return bytes == "Edited original\n";
            }));
            // clang-format on

            if (!languageScenario) {
                product.type("Unsaved ");
                // clang-format off
                ASSERT_TRUE(product.frameUntil([&product, &remoteEditor, editorPage]() { return SurfaceReader(product.declared(editorPage)).properties(*remoteEditor)["value"] == "Edited Unsaved original\n"; }));
                // clang-format on
            }

            if (scenario == ManagerScenario::EditorConflict) {
                std::ofstream(document, std::ios::binary | std::ios::trunc) << "External contents\n";
                const auto documentPath = platform::PathTextHelper::generic(document);
                // clang-format off
                ASSERT_TRUE(product.frameUntil([&product, &documentPath]() {
                    return product.product().shell().toasts().showing("Code Editor", "The file changed outside Workpane while it has unsaved changes. The editor preserved the unsaved buffer\n" + documentPath);
                }));
                // clang-format on

                product.press(HeadlessProduct::command() | ImGuiKey_S);
                // clang-format off
                ASSERT_TRUE(product.frameUntil([&product, &documentPath]() {
                    return product.product().shell().toasts().showing("Code Editor", "The remote file changed since it was opened. Reopen it before saving.\n" + documentPath);
                }));
                // clang-format on

                std::ifstream retained(document);
                EXPECT_EQ(std::string(std::istreambuf_iterator<char>(retained), std::istreambuf_iterator<char>()), "External contents\n");
                retained.close();
                const SurfaceReader conflicted(product.declared(editorPage));
                EXPECT_EQ(conflicted.properties(*remoteEditor)["value"], "Edited Unsaved original\n");
                std::optional<ui::NodeId> encoding;

                for (const auto node : conflicted.nodes("menuButton")) {
                    if (conflicted.properties(node)["text"] == "UTF-8") {
                        encoding = node;
                    }
                }

                ASSERT_TRUE(encoding.has_value());
                product.emit(editorPage, *encoding, "select", {{"item", "reopen:utf-8"}});
                // clang-format off
                ASSERT_TRUE(product.frameUntil([&product]() { return product.product().shell().dialogs().active(); }));
                // clang-format on
                ASSERT_TRUE(product.answerDialog("confirm"));
                // clang-format off
                ASSERT_TRUE(product.frameUntil([&product, &remoteEditor, editorPage]() { return SurfaceReader(product.declared(editorPage)).properties(*remoteEditor)["value"] == "External contents\n"; }));
                // clang-format on

                product.frame();
                product.press(HeadlessProduct::command() | ImGuiKey_A);
                product.type("Resolved contents\n");
                product.press(HeadlessProduct::command() | ImGuiKey_S);
                // clang-format off
                ASSERT_TRUE(product.frameUntil([&document]() {
                    std::ifstream resolved(document);

                    return std::string(std::istreambuf_iterator<char>(resolved), std::istreambuf_iterator<char>()) == "Resolved contents\n";
                }));
                // clang-format on
            }

            EXPECT_TRUE(product.query("SELECT root_path FROM code_editor__workspaces").empty());

            if (languageScenario) {
                setConsumerEnabled(product, "code-editor", "workspace.remote.open", false);
                ASSERT_FALSE(::testing::Test::HasFatalFailure());
                // clang-format off
                ASSERT_TRUE(product.frameUntil([this, &product, editorPage]() { return product.surface(editorPage) == nullptr && !std::filesystem::exists(m_root / ".server-running"); }));
                // clang-format on

                const SurfaceReader connections(product.declared(page));
                EXPECT_EQ(connections.properties(grid)["rows"][0]["cells"][4], "Connected");
                EXPECT_TRUE(std::filesystem::exists(m_root / ".server-shutdown"));
                EXPECT_TRUE(std::filesystem::exists(m_root / ".server-exit"));
                EXPECT_TRUE(product.query("SELECT details_json FROM logs__entries WHERE source = 'code-editor' AND level = 'error'").empty());
            }
        }

        if (scenario == ManagerScenario::Idle) {
            const auto resumed = std::chrono::steady_clock::now() + std::chrono::seconds(65);
            // clang-format off
            ASSERT_TRUE(product.hiddenUntil([&resumed]() { return std::chrono::steady_clock::now() >= resumed; }, std::chrono::seconds(70)));
            // clang-format on

            const SurfaceReader reader(product.declared(page));
            EXPECT_EQ(reader.properties(reader.nodes("table")[0])["rows"][0]["cells"][4], "Connected");
        }

        std::filesystem::path terminalHistory;

        if (terminalScenario) {
            product.emit(page, grid, "action", {{"id", "development"}, {"action", "terminal"}});
            // clang-format off
            ASSERT_TRUE(product.frameUntil([&product, page]() { return !SurfaceReader(product.declared(page)).nodes("terminal").empty(); }));
            // clang-format on

            const auto terminal = SurfaceReader(product.declared(page)).nodes("terminal")[0];
            EXPECT_EQ(SurfaceReader(product.declared(page)).properties(terminal)["transport"], "stream");
            // clang-format off
            const auto awaitPrompt = [&product, page](ui::NodeId node) {
                const auto ready = [&product, page, node]() {
                    auto& surfaces = product.product().surfaces();
                    auto& render = product.product().render();
                    const auto found = surfaces.command(page, "remote-ssh", node, "find", {{"text", "workpane-native-ready>"}}, render);
                    const auto copied = surfaces.command(page, "remote-ssh", node, "copy", nlohmann::json::object(), render);
                    const char* clipboard = ImGui::GetClipboardText();

                    return found.hasValue() && copied.hasValue() && clipboard != nullptr && std::string(clipboard) == "workpane-native-ready>" && ImGui::GetInputTextState(ImGui::GetActiveID()) != nullptr;
                };

                if (!product.frameUntil(ready)) {
                    return false;
                }

                product.press(ImGuiKey_Escape);

                return true;
            };
            // clang-format on

            ASSERT_TRUE(awaitPrompt(terminal));
            product.type("cd " + quoted(platform::PathTextHelper::generic(m_root)) + " && test -t 0 && printf '\\033[31m%s%s\\033[0m\\n' remote-visible- colour && printf '%s\\n' \"$HISTFILE\" > terminal-history && printf ready > terminal-ready");
            product.press(ImGuiKey_Enter);
            // clang-format off
            ASSERT_TRUE(product.frameUntil([this]() { return std::filesystem::exists(m_root / "terminal-ready"); }));
            // clang-format on

            std::ifstream historyFile(m_root / "terminal-history");
            std::string historyPath;
            std::getline(historyFile, historyPath);
            terminalHistory = std::filesystem::path(std::u8string(historyPath.begin(), historyPath.end()));
            ASSERT_FALSE(terminalHistory.empty());
            EXPECT_EQ(std::filesystem::status(terminalHistory.parent_path()).permissions() & std::filesystem::perms::all, std::filesystem::perms::owner_all);
            product.emit(page, terminal, "resize-stream", {{"generation", 1}, {"columns", 97}, {"rows", 33}});
            product.type("PS1=$(printf '%s%s' remote-interrupted- prompt); stty size > terminal-size && sh -c 'printf ready > terminal-sleep; exec sleep 30' && printf unexpected > terminal-uninterrupted");
            product.press(ImGuiKey_Enter);
            // clang-format off
            ASSERT_TRUE(product.frameUntil([this]() { return std::filesystem::exists(m_root / "terminal-sleep"); }));
            // clang-format on

            product.press(ImGuiMod_Ctrl | ImGuiKey_C);
            auto& surfaces = product.product().surfaces();
            auto& render = product.product().render();
            // clang-format off
            ASSERT_TRUE(product.frameUntil([&surfaces, &render, terminal, page]() {
                const auto found = surfaces.command(page, "remote-ssh", terminal, "find", {{"text", "remote-interrupted-prompt"}}, render);
                const auto copied = surfaces.command(page, "remote-ssh", terminal, "copy", nlohmann::json::object(), render);
                const char* copiedText = ImGui::GetClipboardText();
                const bool typing = ImGui::GetInputTextState(ImGui::GetActiveID()) != nullptr;

                return found.hasValue() && copied.hasValue() && copiedText != nullptr && std::string(copiedText) == "remote-interrupted-prompt" && typing;
            }));
            // clang-format on

            product.press(ImGuiKey_Escape);
            product.type("printf resumed > terminal-resumed");
            product.press(ImGuiKey_Enter);
            // clang-format off
            ASSERT_TRUE(product.frameUntil([this]() { return std::filesystem::exists(m_root / "terminal-resumed"); }));
            // clang-format on

            EXPECT_FALSE(std::filesystem::exists(m_root / "terminal-uninterrupted"));
            std::ifstream dimensions(m_root / "terminal-size");
            std::string size;
            std::getline(dimensions, size);
            EXPECT_EQ(size, "33 97");
            // clang-format off
            ASSERT_TRUE(product.frameUntil([&surfaces, &render, terminal, page]() {
                const auto found = surfaces.command(page, "remote-ssh", terminal, "find", {{"text", "remote-visible-colour"}}, render);
                const auto copied = surfaces.command(page, "remote-ssh", terminal, "copy", nlohmann::json::object(), render);
                const char* copiedText = ImGui::GetClipboardText();
                const bool typing = ImGui::GetInputTextState(ImGui::GetActiveID()) != nullptr;

                return found.hasValue() && copied.hasValue() && copiedText != nullptr && std::string(copiedText) == "remote-visible-colour" && typing;
            }));
            // clang-format on

            product.press(ImGuiKey_Escape);
            const SurfaceReader controls(product.declared(page));
            const auto buttons = controls.nodes("button");
            // clang-format off
            const auto restart = std::ranges::find_if(buttons, [&controls](ui::NodeId node) { return controls.properties(node).value("icon", "") == "refresh"; });
            // clang-format on
            ASSERT_NE(restart, buttons.end());
            product.emit(page, *restart, "click", nlohmann::json::object());
            ASSERT_TRUE(awaitPrompt(terminal));
            product.type("cd " + quoted(platform::PathTextHelper::generic(m_root)) + " && { printf '%s\\n' \"$HISTFILE\" > terminal-history-restarted; history > terminal-history-state; fc -ln 1 > terminal-history-entries; printf '%s' \"$?\" > terminal-history-status; printf restarted > terminal-restarted; }");
            product.press(ImGuiKey_Enter);
            // clang-format off
            ASSERT_TRUE(product.frameUntil([this]() { return std::filesystem::exists(m_root / "terminal-restarted"); }));
            // clang-format on

            std::ifstream restartedFile(m_root / "terminal-history-restarted");
            std::string restartedPath;
            std::getline(restartedFile, restartedPath);
            EXPECT_EQ(restartedPath, historyPath);
            std::ifstream historyStatus(m_root / "terminal-history-status");
            std::ifstream historyState(m_root / "terminal-history-state");
            EXPECT_EQ(std::string(std::istreambuf_iterator<char>(historyStatus), std::istreambuf_iterator<char>()), "0") << std::string(std::istreambuf_iterator<char>(historyState), std::istreambuf_iterator<char>());
            std::ifstream historyEntries(m_root / "terminal-history-entries");
            const std::string entries{std::istreambuf_iterator<char>(historyEntries), std::istreambuf_iterator<char>()};
            EXPECT_NE(entries.find("terminal-resumed"), std::string::npos);
            // clang-format off
            const auto add = std::ranges::find_if(buttons, [&controls](ui::NodeId node) { const auto properties = controls.properties(node); return properties.value("icon", "") == "add" && properties.value("variant", "") == "toolbar"; });
            // clang-format on
            ASSERT_NE(add, buttons.end());
            product.emit(page, *add, "click", nlohmann::json::object());
            // clang-format off
            ASSERT_TRUE(product.frameUntil([&product, page]() { return SurfaceReader(product.declared(page)).nodes("terminal").size() == 2; }));
            // clang-format on

            ASSERT_TRUE(awaitPrompt(SurfaceReader(product.declared(page)).nodes("terminal")[1]));
            product.type("cd " + quoted(platform::PathTextHelper::generic(m_root)) + " && printf second > terminal-second");
            product.press(ImGuiKey_Enter);
            // clang-format off
            ASSERT_TRUE(product.frameUntil([this]() { return std::filesystem::exists(m_root / "terminal-second"); }));
            // clang-format on

            const SurfaceReader twoTerminals(product.declared(page));
            const auto strips = twoTerminals.nodes("tabs");
            // clang-format off
            const auto strip = std::ranges::find_if(strips, [&twoTerminals](ui::NodeId node) { const auto properties = twoTerminals.properties(node); return properties.value("closable", false) && properties["items"].size() == 2 && properties["items"][0]["id"] == "1"; });
            // clang-format on
            ASSERT_NE(strip, strips.end());
            ASSERT_TRUE(surfaces.patch(page, "remote-ssh", *strip, {{"current", "1"}}).hasValue());
            product.emit(page, *strip, "select", {{"id", "1"}});
            product.frame();
            product.type("printf selected > terminal-selected");
            product.press(ImGuiKey_Enter);
            // clang-format off
            ASSERT_TRUE(product.frameUntil([this]() { return std::filesystem::exists(m_root / "terminal-selected"); }));
            // clang-format on

            EXPECT_EQ(SurfaceReader(product.declared(page)).nodes("terminal")[0], terminal);
            product.emit(page, *strip, "close", {{"id", "2"}});
            // clang-format off
            ASSERT_TRUE(product.frameUntil([&product, page]() { return SurfaceReader(product.declared(page)).nodes("terminal").size() == 1; }));
            // clang-format on

            product.emit(page, *strip, "close", {{"id", "1"}});
            // clang-format off
            ASSERT_TRUE(product.frameUntil([&product, page]() { return SurfaceReader(product.declared(page)).nodes("terminal").empty(); }));
            // clang-format on

            product.emit(page, *add, "click", nlohmann::json::object());
            // clang-format off
            ASSERT_TRUE(product.frameUntil([&product, page]() { return SurfaceReader(product.declared(page)).nodes("terminal").size() == 1; }));
            // clang-format on

            product.type("cd " + quoted(platform::PathTextHelper::generic(m_root)) + " && printf reopened > terminal-reopened");
            product.press(ImGuiKey_Enter);
            // clang-format off
            ASSERT_TRUE(product.frameUntil([this]() { return std::filesystem::exists(m_root / "terminal-reopened"); }));
            // clang-format on
        }

        if (scenario == ManagerScenario::Stop || scenario == ManagerScenario::TerminalStop || scenario == ManagerScenario::PortsStop || scenario == ManagerScenario::TaskStop) {
            product.recordCalls("workpane_process_write");
            product.stop();
            EXPECT_EQ(product.lastCall("workpane_process_write")["plugin"], "remote-ssh");
            EXPECT_EQ(product.lastCall("workpane_process_write")["content"], "cXVpdAo=");
            const auto cleanupDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

            while (!sessionScopesGone() && std::chrono::steady_clock::now() < cleanupDeadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            EXPECT_TRUE(sessionScopesGone());

            if (scenario == ManagerScenario::TaskStop) {
                EXPECT_EQ(product.query("SELECT status FROM ai__executions")[0]["status"], "running");
                EXPECT_TRUE(product.query("SELECT message FROM logs__entries WHERE source = 'ai' AND level = 'error'").empty());
            }

            if (terminalScenario) {
                EXPECT_FALSE(std::filesystem::exists(terminalHistory.parent_path()));
            }

            if (portsScenario) {
                httplib::Client closed("127.0.0.1", forwardedPort);
                closed.set_connection_timeout(1);
                EXPECT_FALSE(closed.Get("/forwarded"));
            }

            return;
        }

        const bool lost = scenario == ManagerScenario::Lost || scenario == ManagerScenario::TerminalLost || scenario == ManagerScenario::PortsLost || scenario == ManagerScenario::TaskLost;

        if (lost) {
            ASSERT_TRUE(product.product().runtime().loadString("require('workpane.process').stop('remote-ssh', " + connectionProcess.dump() + ")", "ssh-disconnect").hasValue());
        } else {
            product.emit(page, grid, "action", {{"id", "development"}, {"action", "disconnect"}});
        }

        const std::string expected = lost ? "Failed" : "Disconnected";
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&product, &expected, page]() {
            const SurfaceReader reader(product.declared(page));

            return reader.properties(reader.nodes("table")[0])["rows"][0]["cells"][4] == expected;
        }));
        // clang-format on

        // clang-format off
        EXPECT_TRUE(product.frameUntil([this]() { return sessionScopesGone(); }));
        // clang-format on
        EXPECT_TRUE(product.query("SELECT details_json FROM logs__entries WHERE source = 'remote-ssh' AND level = 'error'").empty());

        if (scenario == ManagerScenario::TaskLost) {
            // clang-format off
            ASSERT_TRUE(product.frameUntil([&product]() { return product.query("SELECT status FROM ai__executions")[0]["status"] == "failed"; }));
            // clang-format on
            EXPECT_EQ(product.query("SELECT error_message FROM ai__executions")[0]["error_message"], "The remote workspace disconnected or its saved profile is unavailable");
        }

        if (scenario == ManagerScenario::Task) {
            ASSERT_TRUE(product.navigate("ai:tasks").hasValue());
            taskAction(product, "ai.task.start");
            ASSERT_FALSE(::testing::Test::HasFatalFailure());
            // clang-format off
            ASSERT_TRUE(product.frameUntil([&product]() { const auto rows = product.query("SELECT status FROM ai__executions ORDER BY started_at_utc DESC LIMIT 1"); return product.query("SELECT id FROM ai__executions").size() == 2U && rows[0]["status"] == "failed"; }));
            // clang-format on
            EXPECT_EQ(product.query("SELECT error_message FROM ai__executions ORDER BY started_at_utc DESC LIMIT 1")[0]["error_message"], "The remote workspace disconnected or its saved profile is unavailable");
        }

        if (portsScenario) {
            EXPECT_FALSE(forwardedRequest(product, forwardedPort));
        }

        if (scenario == ManagerScenario::PortsReconnect) {
            product.emit(page, grid, "action", {{"id", "development"}, {"action", "connect"}});
            // clang-format off
            ASSERT_TRUE(product.frameUntil([&product, page, grid]() { return SurfaceReader(product.declared(page)).properties(grid)["rows"][0]["cells"][4] == "Connected"; }));
            // clang-format on

            verifyPorts(product, page, grid, forwardedPort);
            ASSERT_FALSE(::testing::Test::HasFatalFailure());
            const SurfaceReader reconnected(product.declared(page));
            const auto ports = reconnected.nodes("table")[1];
            EXPECT_NE(reconnected.properties(ports)["rows"][0]["id"], previousForward);
            product.emit(page, ports, "action", {{"id", previousForward}, {"action", "close"}});
            EXPECT_TRUE(forwardedRequest(product, forwardedPort));
            product.emit(page, grid, "action", {{"id", "development"}, {"action", "disconnect"}});
            // clang-format off
            ASSERT_TRUE(product.frameUntil([&product, page, grid]() { return SurfaceReader(product.declared(page)).properties(grid)["rows"][0]["cells"][4] == "Disconnected"; }));
            // clang-format on

            EXPECT_FALSE(forwardedRequest(product, forwardedPort));
            EXPECT_TRUE(product.query("SELECT details_json FROM logs__entries WHERE source = 'remote-ssh' AND level = 'error'").empty());
        }

        if (scenario == ManagerScenario::Editor || scenario == ManagerScenario::EditorReconnect) {
            // clang-format off
            ASSERT_TRUE(product.frameUntil([&product, editorPage]() {
                const SurfaceReader reader(product.declared(editorPage));
                const auto alert = reader.nodeShowing("code-editor.remote.disconnected");

                return alert.has_value() && reader.properties(*alert)["visible"] == true;
            }));
            // clang-format on

            EXPECT_EQ(SurfaceReader(product.declared(editorPage)).properties(*remoteEditor)["value"], "Edited Unsaved original\n");
            EXPECT_TRUE(product.query("SELECT details_json FROM logs__entries WHERE source = 'code-editor' AND level = 'error'").empty());

            if (scenario == ManagerScenario::EditorReconnect) {
                verifyEditorReconnect(product, page, grid, editorPage, *remoteEditor);
                ASSERT_FALSE(::testing::Test::HasFatalFailure());
            }
        }

        if (terminalScenario) {
            EXPECT_TRUE(SurfaceReader(product.declared(page)).nodes("terminal").empty());
            // clang-format off
            EXPECT_TRUE(product.frameUntil([&terminalHistory]() { return !std::filesystem::exists(terminalHistory.parent_path()); }, std::chrono::seconds(5)));
            // clang-format on
        }

        product.stop();

        EXPECT_TRUE(sessionScopesGone());
    }

    void verifyNativeProfileAccounts() {
        for (const auto& account : std::array<std::string, 4>{"Reader Name", "reader@example.org", "café", "reader%name"}) {
            const nlohmann::json profile{{"id", "profile"}, {"name", "Development"}, {"host", "configured-alias"}, {"user", account}, {"port", 0}, {"identity", ""}, {"configuration", "/dev/null"}, {"root", path("")}, {"timeoutSeconds", 5}, {"savePassword", false}};
            const auto prepared = module("async.run(function() local profile = require('json').decode(" + nlohmann::json(profile.dump()).dump() + ") host.test_result({ arguments = assert(environment.include('profiles').arguments(profile)) }) end)");
            ASSERT_TRUE(prepared.contains("arguments"));
            auto options = prepared["arguments"].get<std::vector<std::string>>();
            options.insert(options.end(), {"-G", "configured-alias"});
            const auto output = std::make_shared<Output>();
            auto program = start(m_ssh, std::move(options), "", output);
            ASSERT_NE(program, nullptr);
            std::unique_lock lock(output->mutex);
            // clang-format off
            const bool ended = output->changed.wait_for(lock, std::chrono::seconds(10), [&output]() { return output->end.has_value(); });
            // clang-format on
            lock.unlock();

            if (!ended) {
                program->stop();
            }

            ASSERT_TRUE(ended);
            EXPECT_EQ(output->end->code, 0) << output->text;
            EXPECT_NE(output->text.find("user " + account + "\n"), std::string::npos) << output->text;
            EXPECT_NE(output->text.find("hostname configured-alias\n"), std::string::npos) << output->text;
        }
    }

    void verifyNativeIpv6Transfer() {
        const auto port = LocalPort::free();
        ASSERT_GT(port, 0);
        const auto daemonOutput = std::make_shared<Output>();
        const auto daemon = m_launcher.find("sshd", {"/usr/sbin", "/usr/lib/openssh"});
        ASSERT_TRUE(daemon.has_value());
        auto server = start(*daemon, {"-D", "-e", "-f", path("sshd_config"), "-p", std::to_string(port), "-o", "ListenAddress=::1"}, "", daemonOutput);
        ASSERT_NE(server, nullptr);

        {
            std::unique_lock lock(daemonOutput->mutex);
            // clang-format off
            ASSERT_TRUE(daemonOutput->changed.wait_for(lock, std::chrono::seconds(10), [&daemonOutput]() { return daemonOutput->end.has_value() || daemonOutput->text.find("Server listening on ::1") != std::string::npos; }));
            // clang-format on
            ASSERT_FALSE(daemonOutput->end.has_value()) << daemonOutput->text;
        }

        std::ifstream key(path("host.pub"));
        std::string algorithm;
        std::string encoded;
        key >> algorithm >> encoded;
        std::ofstream(path("known_ipv6")) << "[::1]:" << port << " " << algorithm << " " << encoded << "\n";
        std::ofstream configuration(path("client_ipv6_config"));
        configuration << "Host ::1\n    UserKnownHostsFile " << std::quoted(path("known_ipv6")) << "\n    GlobalKnownHostsFile /dev/null\n    IdentitiesOnly yes\n";
        configuration.close();
        auto options = arguments("client", 0);
        const auto portOption = std::ranges::find(options, "-p");
        ASSERT_NE(portOption, options.end());
        *(portOption + 1) = std::to_string(port);
        const auto trust = std::ranges::find(options, "UserKnownHostsFile=" + path("known_hosts"));
        ASSERT_NE(trust, options.end());
        *trust = "UserKnownHostsFile=" + path("known_ipv6");
        options.back() = "::1";
        SftpProbeHelper::verify(m_ssh, std::move(options), m_root);
    }

  private:
    [[nodiscard]] static std::string quoted(std::string_view source) {
        std::string result{"'"};

        for (const char character : source) {
            result += character == '\'' ? "'\\''" : std::string(1, character);
        }

        return result + "'";
    }

    void generateKey(const std::filesystem::path& keygen, std::string_view name) {
        const auto output = std::make_shared<Output>();
        auto program = start(keygen, {"-q", "-t", "ed25519", "-N", "", "-f", path(name)}, "", output);
        ASSERT_NE(program, nullptr);
        std::unique_lock lock(output->mutex);
        // clang-format off
        const bool ended = output->changed.wait_for(lock, std::chrono::seconds(10), [&output]() { return output->end.has_value(); });
        // clang-format on
        lock.unlock();

        if (!ended) {
            program->stop();
        }

        ASSERT_TRUE(ended);
        EXPECT_EQ(output->end->code, 0) << output->text;
    }

    std::unique_ptr<TemporaryDirectory> m_directory;
    platform::NativeProcesses m_launcher;
    std::filesystem::path m_root;
    std::filesystem::path m_ssh;
    std::string m_user;
    int m_port{0};
    int m_forwardPort{0};
    std::shared_ptr<Output> m_daemonOutput;
    std::shared_ptr<Output> m_connectionOutput;
    std::unique_ptr<process::Process> m_daemon;
    std::unique_ptr<process::Process> m_connection;
    httplib::Server m_forwardServer;
    std::thread m_forwardThread;
    std::filesystem::path m_sessionHome;
    nlohmann::json m_sessionProfile;
};

TEST_F(RemoteSshIntegrationTest, TransfersBinaryFilesThroughTheNativeSftpSubsystemAndProductProcessBridge) {
    SftpProbeHelper::verify(ssh(), arguments("client", 0), std::filesystem::canonical(path("")));
}

TEST_F(RemoteSshIntegrationTest, RecyclesOwnedProgramsAndPreservesTheirBinaryStreamsAndActualExitThroughTheProduct) {
    SshScopeProbeHelper::verify(ssh(), arguments("client", 0), std::filesystem::canonical(path("")));
}

TEST_F(RemoteSshIntegrationTest, RunsAnInteractiveSshTerminalWithNativeResizeAndInterruptionThroughTheProduct) {
    SshTerminalProbeHelper::verify(ssh(), arguments("client", 0), arguments("client", 0), arguments("client", 0), std::filesystem::canonical(path("")));
}

#if defined(__APPLE__)
TEST_F(RemoteSshIntegrationTest, StopsOwnedSessionDescendantsInDifferentGroupsThroughNativeMacApisOverSsh) {
    verifyNativeMacSessionCleanup();
}
#endif

TEST_F(RemoteSshIntegrationTest, SelectsAProfileFolderThroughATemporaryNativeSshConnection) {
    connectManager(ManagerScenario::ProfileFolder);
}

TEST_F(RemoteSshIntegrationTest, CancelsAProfileFolderSelectionWhileItsNativeConnectionPrepares) {
    connectManager(ManagerScenario::ProfileFolderCancel);
}

TEST_F(RemoteSshIntegrationTest, ReleasesAProfileFolderConnectionWhenItsNativeCredentialPromptIsCancelled) {
    connectManager(ManagerScenario::ProfileFolderCredentialsCancel);
}

TEST_F(RemoteSshIntegrationTest, RunsASavedRemoteTaskThroughTheBoardAndRefusesDisconnectedExecution) {
    connectManager(ManagerScenario::Task);
}

TEST_F(RemoteSshIntegrationTest, RunsAnAgentWithRemoteInstructionsFilesToolsAndCommandsThroughTheBoard) {
    connectManager(ManagerScenario::TaskAgent);
}

TEST_F(RemoteSshIntegrationTest, RunsARemoteCommandLineAgentWithItsPromptOnInputAndPrivateDiagnostics) {
    connectManager(ManagerScenario::TaskCli);
}

TEST_F(RemoteSshIntegrationTest, CancelsASavedRemoteTaskAndReleasesItsProgramGroup) {
    connectManager(ManagerScenario::TaskCancel);
}

TEST_F(RemoteSshIntegrationTest, EndsARemoteTaskWithAKnownFailureWhenItsSshConnectionEnds) {
    connectManager(ManagerScenario::TaskLost);
}

TEST_F(RemoteSshIntegrationTest, StopsTheProductWithASavedRemoteTaskRunning) {
    connectManager(ManagerScenario::TaskStop);
}

TEST_F(RemoteSshIntegrationTest, PreservesProfileAccountNamesThroughTheNativeOpenSshParser) {
    verifyNativeProfileAccounts();
}

TEST_F(RemoteSshIntegrationTest, TransfersBinaryFilesThroughTheNativeIpv6SftpSubsystem) {
    verifyNativeIpv6Transfer();
}

TEST_F(RemoteSshIntegrationTest, RunsTheProductionSessionThroughTheNativeSdkAndOpenSsh) {
    connectSession();
    EXPECT_FALSE(::testing::Test::HasFailure());
}

TEST_F(RemoteSshIntegrationTest, ForwardsAndClosesBinaryHttpThroughTheActualProfileAndPortControls) {
    connectManager(ManagerScenario::Ports);
}

TEST_F(RemoteSshIntegrationTest, ClosesTheForwardedListenerWhenTheActualSshConnectionIsLost) {
    connectManager(ManagerScenario::PortsLost);
}

TEST_F(RemoteSshIntegrationTest, ClosesForwardedListenersBeforeTheProductStopsItsSshConnection) {
    connectManager(ManagerScenario::PortsStop);
}

TEST_F(RemoteSshIntegrationTest, ReconnectsActualSshForwardingWithNewPortsAndRefusesAStaleClose) {
    connectManager(ManagerScenario::PortsReconnect);
}

TEST_F(RemoteSshIntegrationTest, ConnectsAndDisconnectsThroughTheActualProfileActions) {
    connectManager(ManagerScenario::Disconnect);
    EXPECT_FALSE(::testing::Test::HasFailure());
}

TEST_F(RemoteSshIntegrationTest, EditsAndSavesRemoteFilesThroughTheActualProfileAndPreservesUnsavedTextOnDisconnect) {
    connectManager(ManagerScenario::Editor);
}

TEST_F(RemoteSshIntegrationTest, RunsARemoteLanguageServerAndReleasesItWhenTheConsumingEditorIsDisabled) {
    connectManager(ManagerScenario::EditorLanguage);
}

TEST_F(RemoteSshIntegrationTest, RefusesToOverwriteAnExternalEditAndAcceptsItsRevisionAfterReopening) {
    connectManager(ManagerScenario::EditorConflict);
}

TEST_F(RemoteSshIntegrationTest, ReconnectsWithANewEditorWorkspaceAndRefusesReadsAndStartsFromThePreviousScope) {
    connectManager(ManagerScenario::EditorReconnect);
}

TEST_F(RemoteSshIntegrationTest, RunsAnInteractiveTerminalThroughTheActualProfileAndComponent) {
    connectManager(ManagerScenario::Terminal);
}

TEST_F(RemoteSshIntegrationTest, PreservesBashStartupAndPrivateHistoryAcrossANativeRemoteTerminalRestart) {
    connectManager(ManagerScenario::TerminalBash);
}

TEST_F(RemoteSshIntegrationTest, ReleasesTerminalHistoryAfterItsNativeSshConnectionIsLost) {
    connectManager(ManagerScenario::TerminalLost);
}

TEST_F(RemoteSshIntegrationTest, ClosesTheProductWithAnInteractiveRemoteTerminalStillActive) {
    connectManager(ManagerScenario::TerminalStop);
}

TEST_F(RemoteSshIntegrationTest, ReportsAConnectionLostAfterItsNativeSshProcessEnds) {
    connectManager(ManagerScenario::Lost);
    EXPECT_FALSE(::testing::Test::HasFailure());
}

TEST_F(RemoteSshIntegrationTest, AcceptsHostTrustThroughTheActualAuthenticationDialog) {
    connectManager(ManagerScenario::Trust);
    EXPECT_FALSE(::testing::Test::HasFailure());
}

TEST_F(RemoteSshIntegrationTest, CancelsTheConnectionWhenHostTrustIsDeclined) {
    connectManager(ManagerScenario::DeclinedTrust);
    EXPECT_FALSE(::testing::Test::HasFailure());
}

TEST_F(RemoteSshIntegrationTest, UnlocksAnEncryptedKeyForTheControlAndSftpConnections) {
    connectManager(ManagerScenario::Password);
    EXPECT_FALSE(::testing::Test::HasFailure());
}

TEST_F(RemoteSshIntegrationTest, StopsAnActiveProfileConnectionWhenTheProductCloses) {
    connectManager(ManagerScenario::Stop);
    EXPECT_FALSE(::testing::Test::HasFailure());
}

TEST_F(RemoteSshIntegrationTest, RenewsAnIdleConnectionBeyondItsLeaseWhileTheProductIsHidden) {
    connectManager(ManagerScenario::Idle);
    EXPECT_FALSE(::testing::Test::HasFailure());
}

TEST_F(RemoteSshIntegrationTest, RefusesAnUntrustedClientKeyBeforeExecutingACommand) {
    auto options = arguments("host", 0);
    options.push_back("printf should-not-run");
    const auto output = std::make_shared<Output>();
    auto program = start(ssh(), std::move(options), "", output);
    ASSERT_NE(program, nullptr);
    std::unique_lock lock(output->mutex);
    // clang-format off
    ASSERT_TRUE(output->changed.wait_for(lock, std::chrono::seconds(10), [&output]() { return output->end.has_value(); }));
    // clang-format on
    EXPECT_EQ(output->end->code, 255);
    EXPECT_EQ(output->text.find("should-not-run"), std::string::npos);
}

TEST_F(RemoteSshIntegrationTest, RefusesAChangedHostKeyBeforeExecutingACommand) {
    std::ifstream known(path("known_hosts"));
    std::string address;
    known >> address;
    std::ifstream publicKey(path("client.pub"));
    std::string algorithm;
    std::string encoded;
    publicKey >> algorithm >> encoded;
    std::ofstream(path("known_hosts")) << address << " " << algorithm << " " << encoded << "\n";
    auto options = arguments("client", 0);
    options.push_back("printf should-not-run");
    const auto output = std::make_shared<Output>();
    auto program = start(ssh(), std::move(options), "", output);
    ASSERT_NE(program, nullptr);
    std::unique_lock lock(output->mutex);
    // clang-format off
    ASSERT_TRUE(output->changed.wait_for(lock, std::chrono::seconds(10), [&output]() { return output->end.has_value(); }));
    // clang-format on
    EXPECT_EQ(output->end->code, 255);
    EXPECT_EQ(output->text.find("should-not-run"), std::string::npos);
}

} // namespace workpane::tests
