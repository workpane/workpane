#include "app/Product.h"
#include "localization/Localization.h"
#include "persistence/Database.h"
#include "persistence/DatabaseBootstrap.h"
#include "persistence/PreferenceStore.h"
#include "process/ProcessStream.h"
#include "scripting/PluginRegistry.h"
#include "scripting/ScriptRuntime.h"
#include "support/FakeProvider.h"
#include "support/FileAddress.h"
#include "support/HeadlessProduct.h"
#include "support/LocalPort.h"
#include "support/ProductDatabase.h"
#include "support/RootedPath.h"
#include "support/SurfaceReader.h"
#include "support/TemporaryDirectory.h"
#include "ui/shell/Shell.h"

#include <gtest/gtest.h>
#include <httplib.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

namespace workpane::tests {

using nlohmann::json;

// Drives the AI plugin through its board, its task forms, commands, agents answered by a provider the test plays, their conversations and their settings.
class AiPluginTest : public ::testing::Test {
  protected:
    static constexpr std::string_view view{"view:ai:tasks"};

    void SetUp() override {
        m_root = std::filesystem::canonical(m_folder.path());
        std::ofstream(m_root / "notes.txt", std::ios::binary) << "first line\nsecond line\n";
    }

    [[nodiscard]] const std::filesystem::path& data() const {
        return m_data.path();
    }

    [[nodiscard]] const std::filesystem::path& root() const {
        return m_root;
    }

    // A connection to the provider the test plays, under the key `ollama/fake-large`.
    [[nodiscard]] static json localConnection(const FakeProvider& provider) {
        return {{"providerId", "ollama"}, {"modelId", "fake-large"}, {"displayName", "Local"}, {"apiKey", ""}, {"address", provider.address()}, {"parameters", {{"maxOutputTokens", 1024}, {"temperature", 0.5}, {"topP", 0.95}, {"frequencyPenalty", 0.0}, {"presencePenalty", 0.0}}}, {"extraParameters", ""}};
    }

    [[nodiscard]] static json agent(std::string_view connectionKey, int iterations) {
        return {{"id", "builder"}, {"name", "Builder"}, {"description", "Builds things"}, {"systemPrompt", "You are {{AGENT_NAME}} working in {{TASK_WORKDIR}} on {{TASK_TITLE}}."}, {"connectionKey", connectionKey}, {"maximumIterations", iterations}};
    }

    [[nodiscard]] persistence::Database openDatabase() const {
        return ProductDatabase::open(m_data);
    }

    // The settings document of the plugin is written before the product starts, as a previous session would have left it.
    void store(const json& document) const {
        auto database = ProductDatabase::open(m_data);
        ASSERT_TRUE(persistence::PreferenceStore::store(database, "ai", document).hasValue());
    }

    // The settings name one local connection to the provider the test plays and one agent that runs on it.
    void configure(const FakeProvider& provider, int iterations = 8, const json& servers = json::array()) const {
        store({{"connections", json::array({localConnection(provider)})}, {"defaultConnectionKey", "ollama/fake-large"}, {"agents", json::array({agent("ollama/fake-large", iterations)})}, {"mcpServers", servers}});
    }

    // A server that answers MCP over its standard streams with two tools: lookup, which returns a note about the topic it is given, and hang, which never answers.
    void serveMcp(HeadlessProduct& product) {
        // clang-format off
        product.processes().responder = [this, &product](std::size_t program, std::string_view written) {
            std::string& buffer = m_buffers[program];
            buffer += written;

            for (auto end = buffer.find('\n'); end != std::string::npos; end = buffer.find('\n')) {
                const json message = json::parse(buffer.substr(0, end));
                buffer.erase(0, end + 1);
                m_received.push_back(message);

                const bool unanswered = message.value("method", "") == "tools/call" && message["params"]["name"] == "hang";

                if (!message.contains("id") || !message.contains("method") || unanswered) {
                    continue;
                }

                const std::string method = message["method"];
                json result = json::object();

                if (method == "initialize") {
                    result = {{"protocolVersion", "2025-06-18"}, {"capabilities", {{"tools", json::object()}}}, {"serverInfo", {{"name", "notes"}, {"version", "1"}}}};
                } else if (method == "tools/list") {
                    result = {{"tools", json::array({{{"name", "lookup"}, {"description", "Look up a note"}, {"inputSchema", {{"type", "object"}, {"properties", {{"topic", {{"type", "string"}}}}}, {"required", json::array({"topic"})}}}}, {{"name", "hang"}, {"description", "Never answers"}, {"inputSchema", {{"type", "object"}, {"properties", json::object()}}}}})}};
                } else if (method == "tools/call") {
                    result = {{"content", json::array({{{"type", "text"}, {"text", "Note about " + message["params"]["arguments"]["topic"].get<std::string>()}}})}};
                }

                product.processes().events[program].output(process::ProcessStream::Output, json{{"jsonrpc", "2.0"}, {"id", message["id"]}, {"result", result}}.dump() + "\n");
            }
        };
        // clang-format on
    }

    // The answer the client gave to a request the server sent is the message carrying its identifier and no method.
    [[nodiscard]] json reply(std::string_view id) const {
        for (const auto& message : m_received) {
            if (message.value("id", json()) == json(id) && !message.contains("method")) {
                return message;
            }
        }

        return json();
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

    // Settings are reached by searching for a text of their section, which mounts that section.
    static void openSettings(HeadlessProduct& product, std::string_view query, std::string_view surface) {
        ASSERT_TRUE(product.navigate(ui::Shell::settingsDestination).hasValue());
        product.frame();
        product.frame();
        product.press(HeadlessProduct::command() | ImGuiKey_F);
        product.frame();
        product.press(HeadlessProduct::command() | ImGuiKey_A);
        product.type(query);
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(surface)).mounted(); }));
        // clang-format on
    }

    // An icon button is found by the key of its tooltip.
    static void clickTooltip(HeadlessProduct& product, std::string_view surface, std::string_view tooltip) {
        const SurfaceReader reader(product.declared(surface));

        for (const auto button : reader.nodes("button")) {
            const json props = reader.properties(button);

            if (props.contains("tooltip") && props["tooltip"].value("key", "") == tooltip) {
                product.emit(surface, button, "click", json::object());
                return;
            }
        }

        FAIL() << "No button offers " << tooltip;
    }

    // A command runs through the shell of the platform, the command prompt on Windows and the POSIX shell elsewhere.
    [[nodiscard]] static std::filesystem::path shellProgram() {
#if defined(_WIN32)
        return RootedPath::of("Windows/System32/cmd.exe");
#else
        return "/bin/sh";
#endif
    }

    [[nodiscard]] static std::vector<std::string> shellArguments(std::string command) {
#if defined(_WIN32)
        return {"/d", "/s", "/c", std::move(command)};
#else
        return {"-lc", std::move(command)};
#endif
    }

    static void boot(HeadlessProduct& product) {
        ASSERT_TRUE(product.boot().hasValue());
        ASSERT_TRUE(product.navigate("ai:tasks").hasValue());
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(view)).showsKey("ai.tasks.title"); }));
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

    static void emit(HeadlessProduct& product, std::string_view surface, std::string_view kind, std::size_t index, std::string_view name, json value) {
        const auto found = nodes(product, kind, surface);
        ASSERT_GT(found.size(), index) << kind;
        product.emit(surface, found[index], name, std::move(value));
    }

    // The dialog a plugin opened last is its highest numbered one still mounted, counted among the dialogs of every plugin.
    [[nodiscard]] static std::string dialog(HeadlessProduct& product, std::string_view plugin = "ai") {
        std::string found;

        for (int number = 1; number < 64; ++number) {
            const std::string candidate = "dialog:" + std::string(plugin) + ":" + std::to_string(number);

            if (SurfaceReader(product.declared(candidate)).mounted()) {
                found = candidate;
            }
        }

        return found;
    }

    static void answerPrompt(HeadlessProduct& product, std::string_view value) {
        // clang-format off
        ASSERT_TRUE(product.frameUntil([]() { return ImGui::GetTopMostPopupModal() != nullptr; }));
        // clang-format on
        product.frame();
        product.press(HeadlessProduct::command() | ImGuiKey_A);
        product.type(value);
        ASSERT_TRUE(product.answerDialog("confirm"));
    }

    static void createWorkspace(HeadlessProduct& product, std::string_view name) {
        const std::size_t before = product.query("SELECT id FROM ai__workspaces").size();
        click(product, "ai.workspace.add");
        answerPrompt(product, name);
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT id FROM ai__workspaces").size() == before + 1; }));
        // clang-format on
    }

    // The form lists the title, description, issue, working directory, command and cron fields as text fields, and the kind, agent and schedule as combos.
    [[nodiscard]] static std::string openTaskForm(HeadlessProduct& product) {
        const std::string before = dialog(product);
        clickTooltip(product, view, "ai.task.add");
        // clang-format off
        EXPECT_TRUE(product.frameUntil([&]() { return !dialog(product).empty() && dialog(product) != before; }));
        // clang-format on
        return dialog(product);
    }

    void createCommandTask(HeadlessProduct& product, std::string_view title, std::string_view command) const {
        const std::string form = openTaskForm(product);
        emit(product, form, "combo", 0, "change", {{"value", "command"}});
        emit(product, form, "textField", 0, "change", {{"value", title}});
        emit(product, form, "textField", 3, "change", {{"value", root().generic_string()}});
        emit(product, form, "textField", 4, "change", {{"value", command}});
        product.frame();
        ASSERT_TRUE(product.answerDialog("save"));
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return !product.query("SELECT id FROM ai__tasks WHERE title = ?", {title}).empty(); }));
        // clang-format on
    }

    void createAgentTask(HeadlessProduct& product, std::string_view title, std::string_view prompt) const {
        const std::string form = openTaskForm(product);
        emit(product, form, "textField", 0, "change", {{"value", title}});
        emit(product, form, "textField", 3, "change", {{"value", root().generic_string()}});
        emit(product, form, "textArea", 0, "change", {{"value", prompt}});
        product.frame();
        ASSERT_TRUE(product.answerDialog("save"));
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return !product.query("SELECT id FROM ai__tasks WHERE title = ?", {title}).empty(); }));
        // clang-format on
    }

    [[nodiscard]] static std::string taskId(HeadlessProduct& product, std::string_view title) {
        const auto rows = product.query("SELECT id FROM ai__tasks WHERE title = ?", {title});
        return rows.empty() ? std::string() : rows[0]["id"].get<std::string>();
    }

    // A card is created after its children, so the nodes of a card are those numbered after the card before it and before the card itself.
    [[nodiscard]] static std::optional<ui::NodeId> cardNode(HeadlessProduct& product, std::string_view kind, std::string_view title, std::string_view tooltip) {
        const SurfaceReader reader(product.declared(view));
        ui::NodeId previous{};

        for (const auto card : reader.nodes("card")) {
            if (reader.properties(card).value("drag", json::object()).value("label", "") != title) {
                previous = card;
                continue;
            }

            for (const auto node : reader.nodes(kind)) {
                const json props = reader.properties(node);

                if (node > previous && node < card && props.contains("tooltip") && props["tooltip"].value("key", "") == tooltip && props.value("enabled", true)) {
                    return node;
                }
            }

            return std::nullopt;
        }

        return std::nullopt;
    }

    static void cardAction(HeadlessProduct& product, std::string_view title, std::string_view tooltip) {
        const auto button = cardNode(product, "button", title, tooltip);
        ASSERT_TRUE(button.has_value()) << title << " offers no " << tooltip;
        product.emit(view, *button, "click", json::object());
    }

    // The folder of a task is offered to other plugins from the menu of its card.
    static void cardMenu(HeadlessProduct& product, std::string_view title, std::string_view item) {
        const auto menu = cardNode(product, "menuButton", title, "ai.task.workdir");
        ASSERT_TRUE(menu.has_value()) << title << " offers no folder menu";
        product.emit(view, *menu, "select", {{"item", item}});
    }

    [[nodiscard]] static std::string status(HeadlessProduct& product, std::string_view task) {
        const auto rows = product.query("SELECT status FROM ai__executions WHERE task_id = ? ORDER BY started_at_utc DESC, id DESC LIMIT 1", {task});
        return rows.empty() ? std::string() : rows[0]["status"].get<std::string>();
    }

    [[nodiscard]] static std::vector<json> errors(const HeadlessProduct& product) {
        return product.query("SELECT category, message, details_json FROM logs__entries WHERE level = 'error'");
    }

  private:
    std::map<std::size_t, std::string> m_buffers;
    std::vector<json> m_received;
    TemporaryDirectory m_data;
    TemporaryDirectory m_folder;
    std::filesystem::path m_root;
};

// A workspace holds a board of five columns, a task is written through its form, refused while a rule is broken, moved between columns and kept across a restart.
TEST_F(AiPluginTest, OrganizesWorkspacesAndTasksOnTheBoard) {
    {
        HeadlessProduct product(data());
        boot(product);
        EXPECT_FALSE(nodes(product, "emptyState").empty());
        createWorkspace(product, "Product");
        EXPECT_EQ(product.query("SELECT name FROM ai__workspaces")[0]["name"], "Product");

        // Escape closes a list opened inside the form before it closes the form.
        const std::string form = openTaskForm(product);
        // clang-format off
        ASSERT_TRUE(product.frameUntil([]() { return GImGui->OpenPopupStack.Size == 1; }));
        // clang-format on
        product.frame();

        for (int step = 0; step < 3; ++step) {
            product.press(ImGuiKey_Tab);
        }

        product.press(ImGuiKey_Space);
        EXPECT_EQ(GImGui->OpenPopupStack.Size, 2);
        product.press(ImGuiKey_Escape);
        product.frame();
        EXPECT_EQ(GImGui->OpenPopupStack.Size, 1);
        EXPECT_TRUE(SurfaceReader(product.declared(form)).mounted());

        // An empty title is the first rule the form names, and the title field takes the keyboard.
        ASSERT_TRUE(product.answerDialog("save"));
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(form)).showsKey("ai.validation.title"); }));
        // clang-format on
        product.settle(std::chrono::milliseconds(50));
        product.type("Plan");
        product.settle(std::chrono::milliseconds(50));

        // What was typed became the title, so the next rule is a missing prompt, whose page opens with the prompt taking the keyboard.
        ASSERT_TRUE(product.answerDialog("save"));
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(form)).showsKey("ai.validation.prompt") && properties(product, nodes(product, "tabs", form).front(), form)["current"] == "prompt"; }));
        // clang-format on
        product.settle(std::chrono::milliseconds(50));
        product.type("Write");
        product.settle(std::chrono::milliseconds(50));

        // What was typed became the prompt, so the next rule is the agent nobody configured.
        ASSERT_TRUE(product.answerDialog("save"));
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(form)).showsKey("ai.validation.agent-missing"); }));
        // clang-format on

        // A working directory that names a file is refused like one that does not exist.
        emit(product, form, "textField", 3, "change", {{"value", (root() / "notes.txt").generic_string()}});
        ASSERT_TRUE(product.answerDialog("save"));
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(form)).showsKey("ai.validation.workdir"); }));
        // clang-format on
        ASSERT_TRUE(product.answerDialog("cancel"));
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return !SurfaceReader(product.declared(form)).mounted(); }));
        // clang-format on

        createCommandTask(product, "List files", "ls");
        const auto stored = product.query("SELECT execution_kind, command, workdir, column_name FROM ai__tasks");
        ASSERT_EQ(stored.size(), 1U);
        EXPECT_EQ(stored[0]["execution_kind"], "command");
        EXPECT_EQ(stored[0]["command"], "ls");
        EXPECT_EQ(stored[0]["workdir"], root().generic_string());
        EXPECT_EQ(stored[0]["column_name"], "todo");

        // A card dropped on another column moves there.
        const std::string id = taskId(product, "List files");
        std::vector<ui::NodeId> lanes;

        for (const auto column : nodes(product, "column")) {
            if (properties(product, column).contains("accepts")) {
                lanes.push_back(column);
            }
        }

        ASSERT_EQ(lanes.size(), 5U);
        product.emit(view, lanes[2], "drop", {{"kind", "ai.task"}, {"value", id}});
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT column_name FROM ai__tasks")[0]["column_name"] == "blocked"; }));
        // clang-format on
        product.stop();
        EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    }

    HeadlessProduct product(data());
    boot(product);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(view)).showsText("List files"); }));
    // clang-format on

    // The only workspace offers no close, and closing one of two asks first and takes its tasks with it.
    const std::string workspace = product.query("SELECT id FROM ai__workspaces")[0]["id"];
    EXPECT_FALSE(properties(product, nodes(product, "tabs")[0])["items"][0]["closable"].get<bool>());
    createWorkspace(product, "Research");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return properties(product, nodes(product, "tabs")[0])["items"][0]["closable"].get<bool>(); }));
    // clang-format on
    emit(product, view, "tabs", 0, "close", {{"id", workspace}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([]() { return ImGui::GetTopMostPopupModal() != nullptr; }));
    // clang-format on
    product.frame();
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT id FROM ai__tasks").empty() && product.query("SELECT id FROM ai__workspaces").size() == 1U; }));
    // clang-format on

    // Closing the last workspace anyway is refused with its reason, and the board keeps it.
    const std::string last = product.query("SELECT id FROM ai__workspaces")[0]["id"];
    emit(product, view, "tabs", 0, "close", {{"id", last}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([]() { return ImGui::GetTopMostPopupModal() != nullptr; }));
    // clang-format on
    product.frame();
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().shell().toasts().showing("AI Tasks", "The board keeps at least one workspace"); }));
    // clang-format on
    EXPECT_EQ(product.query("SELECT name FROM ai__workspaces")[0]["name"], "Research");
    product.stop();
}

// A cron schedule keeps the zone it was written in, so its wall clock stays the same after the system moves to another zone.
TEST_F(AiPluginTest, KeepsTheZoneACronScheduleWasWrittenIn) {
    const std::map<std::string, int, std::less<>> offsets{{"America/Sao_Paulo", -10800}, {"Asia/Tokyo", 32400}};

    {
        HeadlessProduct product(data());
        product.system().timeZone = "America/Sao_Paulo";
        product.system().zoneOffsets = offsets;
        boot(product);
        createWorkspace(product, "Nightly");
        const std::string form = openTaskForm(product);
        emit(product, form, "combo", 0, "change", {{"value", "command"}});
        emit(product, form, "textField", 0, "change", {{"value", "Report"}});
        emit(product, form, "textField", 3, "change", {{"value", root().generic_string()}});
        emit(product, form, "textField", 4, "change", {{"value", "report"}});
        emit(product, form, "combo", 2, "change", {{"value", "cron"}});
        emit(product, form, "textField", 5, "change", {{"value", "0 9 * * *"}});
        product.frame();
        ASSERT_TRUE(product.answerDialog("save"));
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT task_id FROM ai__schedules").size() == 1U; }));
        // clang-format on

        const auto schedule = product.query("SELECT time_zone, next_run_at_utc FROM ai__schedules");
        EXPECT_EQ(schedule[0]["time_zone"], "America/Sao_Paulo");
        EXPECT_EQ(schedule[0]["next_run_at_utc"].get<std::string>().substr(10), "T12:00:00.000Z");
        product.stop();
        EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    }

    {
        auto database = openDatabase();
        ASSERT_TRUE(database.run("UPDATE ai__schedules SET next_run_at_utc = '2020-01-01T00:00:00.000Z'").hasValue());
    }

    HeadlessProduct product(data());
    product.system().timeZone = "Asia/Tokyo";
    product.system().zoneOffsets = offsets;
    boot(product);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.processes().launches.size() == 1U && product.query("SELECT last_triggered_at_utc FROM ai__schedules")[0]["last_triggered_at_utc"] != ""; }));
    // clang-format on
    const auto schedule = product.query("SELECT time_zone, next_run_at_utc FROM ai__schedules");
    EXPECT_EQ(schedule[0]["time_zone"], "America/Sao_Paulo");
    EXPECT_EQ(schedule[0]["next_run_at_utc"].get<std::string>().substr(10), "T12:00:00.000Z");
    product.processes().events[0].exited({0, false});
    product.stop();
}

// A command that crashes fails its run with the reason in the language of the reader and keeps what it wrote before.
TEST_F(AiPluginTest, ReportsACommandThatCrashed) {
    HeadlessProduct product(data());
    boot(product);
    createWorkspace(product, "Scripts");
    createCommandTask(product, "Build", "make all");
    const std::string id = taskId(product, "Build");

    cardAction(product, "Build", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.processes().launches.size() == 1U && status(product, id) == "running"; }));
    // clang-format on
    product.processes().events[0].output(process::ProcessStream::Output, "linking\n");
    product.processes().events[0].exited({139, true});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "failed"; }));
    // clang-format on

    const auto execution = product.query("SELECT content, error_message FROM ai__executions WHERE task_id = ?", {id});
    EXPECT_EQ(execution[0]["content"], "linking\n");
    EXPECT_EQ(execution[0]["error_message"], "The command terminated abnormally");
    product.stop();
}

// A command runs through the shell in the working directory with its input closed, and its plain output and exit code become the record of the run.
TEST_F(AiPluginTest, RunsACommandAndRecordsItsOutput) {
    HeadlessProduct product(data());
    boot(product);
    createWorkspace(product, "Scripts");
    createCommandTask(product, "Build", "make all");
    const std::string id = taskId(product, "Build");

    cardAction(product, "Build", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.processes().launches.size() == 1U; }));
    // clang-format on
    const auto& launch = product.processes().launches[0];
    EXPECT_EQ(launch.program, shellProgram());
    EXPECT_EQ(launch.arguments, shellArguments("make all"));
    EXPECT_EQ(launch.directory, root());
    EXPECT_EQ(launch.input, std::optional<std::string>(""));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "running" && product.query("SELECT column_name FROM ai__tasks")[0]["column_name"] == "doing"; }));
    // clang-format on

    product.processes().events[0].output(process::ProcessStream::Output, "\x1b[32mcompiled\x1b[0m\r\n");
    product.processes().events[0].output(process::ProcessStream::Error, "warning: unused\n");
    product.processes().events[0].exited({0, false});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded"; }));
    // clang-format on
    const auto execution = product.query("SELECT content, finish_reason FROM ai__executions WHERE task_id = ?", {id});
    EXPECT_EQ(execution[0]["content"], "compiled\nwarning: unused\n");
    EXPECT_EQ(execution[0]["finish_reason"], "0");
    // The column of the task is written after the run it closes, so it is awaited rather than read at once.
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT column_name FROM ai__tasks")[0]["column_name"] == "done"; }));
    // clang-format on

    // A failing command keeps its output and names the code it ended with.
    cardAction(product, "Build", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.processes().launches.size() == 2U && status(product, id) == "running"; }));
    // clang-format on
    product.processes().events[1].output(process::ProcessStream::Error, "missing target\n");
    product.processes().events[1].exited({2, false});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "failed"; }));
    // clang-format on
    EXPECT_TRUE(SurfaceReader(product.declared(view)).showsKey("ai.task.last-error"));

    // Stopping a running command from its card ends its program and records the run as cancelled.
    cardAction(product, "Build", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.processes().launches.size() == 3U && status(product, id) == "running"; }));
    // clang-format on
    cardAction(product, "Build", "ai.task.stop");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "cancelled" && product.processes().stopped[2]; }));
    // clang-format on

    // Dropping the card on Doing starts it, and dropping the running card elsewhere stops it.
    std::vector<ui::NodeId> lanes;

    for (const auto column : nodes(product, "column")) {
        if (properties(product, column).contains("accepts")) {
            lanes.push_back(column);
        }
    }

    ASSERT_EQ(lanes.size(), 5U);
    product.emit(view, lanes[1], "drop", {{"kind", "ai.task"}, {"value", id}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.processes().launches.size() == 4U && status(product, id) == "running"; }));
    // clang-format on
    product.emit(view, lanes[3], "drop", {{"kind", "ai.task"}, {"value", id}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "cancelled" && product.processes().stopped[3]; }));
    // clang-format on

    // The stop button of the surface of a running task stops it as well.
    cardAction(product, "Build", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.processes().launches.size() == 5U && status(product, id) == "running"; }));
    // clang-format on
    cardAction(product, "Build", "ai.task.info");
    std::string surface;
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { surface = dialog(product); return !surface.empty() && properties(product, *SurfaceReader(product.declared(surface)).nodeShowing("ai.task.stop"), surface).value("visible", false); }));
    // clang-format on
    click(product, "ai.task.stop", surface);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "cancelled" && product.processes().stopped[4]; }));
    // clang-format on
    product.press(ImGuiKey_Escape);
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// An agent sends its instructions and the prompt to the provider, runs the tools the model asks for inside the working directory and records the conversation and the run.
TEST_F(AiPluginTest, RunsAnAgentThatCallsToolsInItsFolder) {
    FakeProvider provider;
    configure(provider);
    provider.script(FakeProvider::calls(json::array({{{"id", "call-1"}, {"name", "read_file"}, {"arguments", {{"path", "notes.txt"}}}}})));
    provider.script(FakeProvider::answer("The notes have two lines."));

    HeadlessProduct product(data());
    boot(product);
    createWorkspace(product, "Research");
    createAgentTask(product, "Summarize", "Summarize the notes.");
    const std::string id = taskId(product, "Summarize");
    cardAction(product, "Summarize", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded"; }));
    // clang-format on

    const auto requests = provider.requests();
    ASSERT_EQ(requests.size(), 2U);
    EXPECT_EQ(requests[0]["model"], "fake-large");
    EXPECT_EQ(requests[0]["max_tokens"], 1024);
    EXPECT_EQ(requests[0]["temperature"], 0.5);
    EXPECT_TRUE(requests[0]["stream"]);
    const json& system = requests[0]["messages"][0];
    EXPECT_EQ(system["role"], "system");
    EXPECT_NE(system["content"].get<std::string>().find("You are Builder working in " + root().generic_string() + " on Summarize."), std::string::npos);
    EXPECT_EQ(requests[0]["messages"][1]["content"], "Summarize the notes.");
    // clang-format off
    const auto tools = requests[0]["tools"];
    EXPECT_TRUE(std::ranges::any_of(tools, [](const json& tool) { return tool["function"]["name"] == "read_file"; }));
    // clang-format on

    // The second request carries the call and what the tool answered.
    const json& messages = requests[1]["messages"];
    ASSERT_GE(messages.size(), 4U);
    EXPECT_EQ(messages[2]["role"], "assistant");
    EXPECT_EQ(messages[2]["tool_calls"][0]["id"], "call-1");
    EXPECT_EQ(messages[3]["role"], "tool");
    EXPECT_EQ(messages[3]["tool_call_id"], "call-1");
    EXPECT_NE(messages[3]["content"].get<std::string>().find("second line"), std::string::npos);

    const auto stored = product.query("SELECT role, content FROM ai__messages WHERE task_id = ? ORDER BY sequence", {id});
    ASSERT_EQ(stored.size(), 4U);
    EXPECT_EQ(stored[0]["role"], "user");
    EXPECT_EQ(stored[3]["content"], "The notes have two lines.");
    const auto execution = product.query("SELECT input_tokens, output_tokens, provider_id, model_id, content FROM ai__executions WHERE task_id = ?", {id});
    EXPECT_EQ(execution[0]["input_tokens"], 240);
    EXPECT_EQ(execution[0]["output_tokens"], 60);
    EXPECT_EQ(execution[0]["provider_id"], "ollama");
    EXPECT_EQ(execution[0]["content"], "The notes have two lines.");
    // The column of the task is written after the run it closes, so it is awaited rather than read at once.
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT column_name FROM ai__tasks")[0]["column_name"] == "done"; }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// The conversation of an agent task shows the turns, and a message typed in it runs another turn with everything said before.
TEST_F(AiPluginTest, ContinuesAConversationFromItsSurface) {
    FakeProvider provider;
    configure(provider);
    provider.script(FakeProvider::answer("Hello there."));
    provider.script(FakeProvider::answer("Second answer."));

    HeadlessProduct product(data());
    boot(product);
    createWorkspace(product, "Chat");
    createAgentTask(product, "Talk", "Say hello.");
    const std::string id = taskId(product, "Talk");
    cardAction(product, "Talk", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded"; }));
    // clang-format on

    cardAction(product, "Talk", "ai.task.chat");
    std::string surface;
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { surface = dialog(product); return !surface.empty() && SurfaceReader(product.declared(surface)).showsText("Hello there."); }));
    // clang-format on

    // The zoom buttons of the chat set the size the conversation is written in, and the bubbles follow it at once.
    click(product, "ai.conversation.zoom-in", surface);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().preferences().document("ai").value("chatFontSize", 0) == 12; }));
    ASSERT_TRUE(product.frameUntil([&]() { return std::ranges::any_of(nodes(product, "markdown", surface), [&](ui::NodeId node) { return properties(product, node, surface).value("fontSize", 0) == 12; }); }));
    // clang-format on

    // The output of the run shows the same answer at the size of the interface, so only the bubbles carry the chat size.
    std::vector<json> sizes;

    for (const auto node : nodes(product, "markdown", surface)) {
        const json props = properties(product, node, surface);

        if (props.contains("fontSize")) {
            sizes.push_back(props["fontSize"]);
        }
    }

    ASSERT_FALSE(sizes.empty());
    // clang-format off
    EXPECT_TRUE(std::ranges::all_of(sizes, [](const json& size) { return size == 12; }));
    // clang-format on

    emit(product, surface, "textArea", 0, "change", {{"value", "And again?"}});
    emit(product, surface, "textArea", 0, "submit", json::object());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(surface)).showsText("Second answer."); }));
    // clang-format on
    const auto requests = provider.requests();
    ASSERT_EQ(requests.size(), 2U);
    const json& messages = requests[1]["messages"];
    ASSERT_EQ(messages.size(), 4U);
    EXPECT_EQ(messages[2]["content"], "Hello there.");
    EXPECT_EQ(messages[3]["content"], "And again?");
    EXPECT_EQ(product.query("SELECT id FROM ai__executions WHERE task_id = ?", {id}).size(), 2U);
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A message the conversation could not keep stays in the composer, so the reader loses nothing they wrote.
TEST_F(AiPluginTest, KeepsTheDraftOfAMessageThatCouldNotBeKept) {
    FakeProvider provider;
    configure(provider);
    provider.script(FakeProvider::answer("Hello there."));

    HeadlessProduct product(data());
    boot(product);
    createWorkspace(product, "Chat");
    createAgentTask(product, "Talk", "Say hello.");
    const std::string id = taskId(product, "Talk");
    cardAction(product, "Talk", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded"; }));
    // clang-format on

    cardAction(product, "Talk", "ai.task.chat");
    std::string surface;
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { surface = dialog(product); return !surface.empty() && SurfaceReader(product.declared(surface)).showsText("Hello there."); }));
    // clang-format on

    {
        auto database = openDatabase();
        ASSERT_TRUE(database.run("CREATE TRIGGER refuse_messages BEFORE INSERT ON ai__messages BEGIN SELECT RAISE(ABORT, 'refused'); END").hasValue());
    }

    emit(product, surface, "textArea", 0, "change", {{"value", "Keep me."}});
    emit(product, surface, "textArea", 0, "submit", json::object());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().shell().toasts().showing("AI Tasks", "The conversation could not be recorded"); }));
    // clang-format on
    product.frame();
    EXPECT_EQ(properties(product, nodes(product, "textArea", surface).front(), surface)["value"], "Keep me.");
    // clang-format off
    EXPECT_TRUE(std::ranges::none_of(nodes(product, "markdown", surface), [&](ui::NodeId node) { return properties(product, node, surface).value("text", json()) == "Keep me."; }));
    // clang-format on
    EXPECT_EQ(provider.requests().size(), 1U);
    product.stop();
    EXPECT_FALSE(errors(product).empty());
}

// While the tools of a turn run, the text the model wrote before calling them shows once, in the bubble of that turn.
TEST_F(AiPluginTest, ShowsTheTextOfATurnOnceWhileItsToolsRun) {
    FakeProvider provider;
    configure(provider);
    FakeProvider::Turn turn = FakeProvider::calls(json::array({{{"id", "call-1"}, {"name", "run_command"}, {"arguments", {{"command", "wait"}}}}}));
    turn.text = "Let me look.";
    provider.script(turn);
    provider.script(FakeProvider::answer("Looked."));

    HeadlessProduct product(data());
    boot(product);
    createWorkspace(product, "Tools");
    createAgentTask(product, "Look", "Look around.");
    const std::string id = taskId(product, "Look");
    cardAction(product, "Look", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.processes().launches.size() == 1U; }));
    // clang-format on

    cardAction(product, "Look", "ai.task.chat");
    std::string surface;
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { surface = dialog(product); return !surface.empty() && SurfaceReader(product.declared(surface)).showsText("Let me look."); }));
    // clang-format on
    product.frame();
    std::size_t shown = 0;

    for (const auto node : nodes(product, "markdown", surface)) {
        const json props = properties(product, node, surface);
        shown += props.contains("text") && props["text"] == "Let me look." && props.value("visible", true) ? 1U : 0U;
    }

    EXPECT_EQ(shown, 1U);
    product.processes().events[0].output(process::ProcessStream::Output, "looked\n");
    product.processes().events[0].exited({0, false});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded" && SurfaceReader(product.declared(surface)).showsText("Looked."); }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A run that changes its phase builds again only its own card, so a board full of other cards costs nothing while one of them runs.
TEST_F(AiPluginTest, BuildsOnlyTheCardOfTheTaskThatRuns) {
    FakeProvider provider;
    configure(provider);
    provider.script(FakeProvider::calls(json::array({{{"id", "call-1"}, {"name", "run_command"}, {"arguments", {{"command", "first"}}}}})));
    provider.script(FakeProvider::calls(json::array({{{"id", "call-2"}, {"name", "run_command"}, {"arguments", {{"command", "second"}}}}})));
    provider.script(FakeProvider::answer("Done."));

    HeadlessProduct product(data());
    boot(product);
    createWorkspace(product, "Busy");

    for (int task = 0; task < 6; ++task) {
        createCommandTask(product, "Idle " + std::to_string(task), "true");
    }

    createAgentTask(product, "Work", "Work.");
    const std::string id = taskId(product, "Work");
    cardAction(product, "Work", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.processes().launches.size() == 1U; }));
    // clang-format on
    product.settle(std::chrono::milliseconds(100));

    // Node identities only grow, so the highest one tells how many nodes the board built since.
    // clang-format off
    const auto newest = [&]() { return product.declared(view)->nodes.rbegin()->first; };
    // clang-format on
    const auto before = newest();
    std::size_t board = 0;

    for (const auto card : nodes(product, "card")) {
        board += properties(product, card).contains("drag") ? 1U : 0U;
    }

    ASSERT_EQ(board, 7U);
    product.processes().events[0].output(process::ProcessStream::Output, "first\n");
    product.processes().events[0].exited({0, false});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.processes().launches.size() == 2U; }));
    // clang-format on
    product.settle(std::chrono::milliseconds(100));
    const auto built = newest() - before;
    const std::size_t total = product.declared(view)->nodes.size();
    EXPECT_LT(built, total) << built << " nodes built for a board of " << total;

    product.processes().events[1].output(process::ProcessStream::Output, "second\n");
    product.processes().events[1].exited({0, false});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded"; }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A long answer streamed in many deltas is told to the chat a few times rather than once per delta, and arrives whole in the conversation and in the run.
TEST_F(AiPluginTest, CoalescesTheTextAModelStreams) {
    std::string answer;

    for (int word = 0; word < 2000; ++word) {
        answer += (word == 0 ? "" : " ") + std::string("w") + std::to_string(word);
    }

    FakeProvider provider;
    configure(provider);
    provider.script(FakeProvider::answer(answer));

    HeadlessProduct product(data());
    boot(product);
    createWorkspace(product, "Stream");
    createAgentTask(product, "Speak", "Speak at length.");
    const std::string id = taskId(product, "Speak");
    cardAction(product, "Speak", "ai.task.chat");
    std::string surface;
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { surface = dialog(product); return !surface.empty() && !nodes(product, "textArea", surface).empty(); }));
    // clang-format on
    const std::size_t before = product.declared(surface)->changes;

    emit(product, surface, "textArea", 0, "change", {{"value", "Go on."}});
    emit(product, surface, "textArea", 0, "submit", json::object());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded" && SurfaceReader(product.declared(surface)).showsText(answer); }));
    // clang-format on
    EXPECT_LT(product.declared(surface)->changes - before, 100U);
    EXPECT_EQ(product.query("SELECT content FROM ai__executions WHERE task_id = ?", {id})[0]["content"], answer);
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A provider that refuses the request fails the run with its reason in the language of the reader, and a provider that is busy is asked again.
TEST_F(AiPluginTest, ReportsWhatTheProviderRefused) {
    FakeProvider provider;
    configure(provider);
    provider.script(FakeProvider::refusal(503, R"({"error":{"type":"overloaded","message":"Try later"}})"));
    provider.script(FakeProvider::answer("Recovered."));
    provider.script(FakeProvider::refusal(400, R"({"error":{"type":"invalid_request","message":"Bad model"}})"));

    HeadlessProduct product(data());
    boot(product);
    createWorkspace(product, "Errors");
    createAgentTask(product, "Retry", "Answer.");
    const std::string id = taskId(product, "Retry");
    cardAction(product, "Retry", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded"; }));
    // clang-format on
    EXPECT_EQ(provider.requests().size(), 2U);

    cardAction(product, "Retry", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "failed"; }));
    // clang-format on
    const auto failed = product.query("SELECT error_message FROM ai__executions WHERE task_id = ? AND status = 'failed'", {id});
    ASSERT_EQ(failed.size(), 1U);
    EXPECT_NE(failed[0]["error_message"].get<std::string>().find("Bad model (type \"invalid_request\")"), std::string::npos);

    // The reason reaches the reader in their language with the words of the provider inside it.
    EXPECT_EQ(failed[0]["error_message"].get<std::string>().rfind("The provider refused the request: ", 0), 0U);
    product.stop();
}

// Another plugin starts a task by its identifier alone, and a request of any other shape is refused.
TEST_F(AiPluginTest, StartsATaskForAnotherPlugin) {
    HeadlessProduct product(data());
    boot(product);
    createWorkspace(product, "Automation");
    createCommandTask(product, "Deploy", "true");
    const std::string id = taskId(product, "Deploy");

    const auto loaded = product.product().runtime().loadString(R"(
        local bridge = require("workpane.bridge")
        local task = require("workpane.task")
        local workpane = require("workpane.api").create("logs", "", { version = "", debug = false, platform = "", architecture = "", paths = { data = "" }, languages = {}, themes = {}, icons = {}, colors = {} })

        task.run("logs", "suite", function()
            local codes = {}

            for _, payload in ipairs({ { taskId = "missing" }, { taskId = ")" +
                                                                   id + R"(", extra = true }, {} }) do
                local _, failure = workpane.capabilities.request("ai.task.start", payload):await()
                codes[#codes + 1] = failure.code
            end

            local answer = workpane.capabilities.request("ai.task.start", { taskId = ")" +
                                                                   id + R"(" }):await()
            bridge.call("workpane_log", { plugin = "logs", level = "info", category = "suite", message = "codes:" .. table.concat(codes, ",") .. ":" .. answer.taskId, details = {} })
        end)
    )",
                                                               "capability");
    ASSERT_TRUE(loaded.hasValue()) << loaded.error().message;
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.query("SELECT message FROM logs__entries WHERE message = ?", {"codes:ai_tasks_task_unknown,ai_tasks_request_invalid,ai_tasks_request_invalid:" + id}).empty(); }));
    ASSERT_TRUE(product.frameUntil([&]() { return product.processes().launches.size() == 1U; }));
    // clang-format on
    product.processes().events[0].exited({0, false});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded"; }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A connection is written through its form: the provider chosen, the models its service publishes offered, the extra parameters checked and a duplicate refused.
TEST_F(AiPluginTest, WritesConnectionsWithTheModelsTheServicePublishes) {
    FakeProvider provider;
    HeadlessProduct product(data());
    boot(product);
    const std::string section = "settings:ai:connections:general";
    openSettings(product, "Default connection", section);
    EXPECT_TRUE(SurfaceReader(product.declared(section)).showsKey("ai.connection.empty"));

    for (int round = 0; round < 2; ++round) {
        click(product, "ai.connection.add", section);
        std::string form;
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { form = dialog(product); return !form.empty(); }));
        // clang-format on
        emit(product, form, "combo", 0, "change", {{"value", "ollama"}});
        product.frame();
        emit(product, form, "textField", 2, "change", {{"value", provider.address()}});
        product.frame();
        clickTooltip(product, form, "ai.settings.refresh-models");
        // clang-format off
        const auto offers = [&](std::string_view model) { const json items = properties(product, nodes(product, "menuButton", form).front(), form)["items"]; return std::ranges::any_of(items, [&](const json& item) { return item["id"] == json(model); }); };
        ASSERT_TRUE(product.frameUntil([&]() { return offers("fake-large") && offers("fake-small"); }));
        // clang-format on
        emit(product, form, "menuButton", 0, "select", {{"item", "fake-small"}});
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return properties(product, nodes(product, "textField", form).front(), form)["value"] == "fake-small"; }));
        // clang-format on

        if (round == 1) {
            ASSERT_TRUE(product.answerDialog("save"));
            // clang-format off
            ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(form)).showsKey("ai.validation.connection-duplicate"); }));
            // clang-format on
            ASSERT_TRUE(product.answerDialog("cancel"));
            break;
        }

        // A value typed for a parameter stays through leaving the model field and through another model that still takes it.
        emit(product, form, "numberField", 1, "change", {{"value", 0.25}});
        emit(product, form, "textField", 0, "blur", json::object());
        emit(product, form, "menuButton", 0, "select", {{"item", "fake-large"}});
        product.frame();
        emit(product, form, "menuButton", 0, "select", {{"item", "fake-small"}});
        product.frame();
        EXPECT_EQ(properties(product, nodes(product, "numberField", form)[1], form)["value"], 0.25);

        emit(product, form, "textArea", 0, "change", {{"value", "{\"options\":"}});
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(form)).showsKey("ai.validation.extra-parameters-syntax"); }));
        // clang-format on

        // A rule only the saving checks, such as a key nested too deep, is named in the language of the reader with the parameter it refused.
        emit(product, form, "textArea", 0, "change", {{"value", "{\"a.b.c.d.e.f.g.h.i.j.k.l.m.n.o.p.q\": 1}"}});
        product.frame();
        ASSERT_TRUE(product.answerDialog("save"));
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(form)).showsKey("ai.validation.extra-parameter-invalid"); }));
        // clang-format on
        EXPECT_EQ(properties(product, nodes(product, "alert", form).front(), form)["text"]["args"][0], "a.b.c.d.e.f.g.h.i.j.k.l.m.n.o.p.q");

        emit(product, form, "textArea", 0, "change", {{"value", "{\"options.num_ctx\": 4096}"}});
        emit(product, form, "textField", 1, "change", {{"value", "Local small"}});
        product.frame();
        ASSERT_TRUE(product.answerDialog("save"));
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return !SurfaceReader(product.declared(form)).mounted(); }));
        // clang-format on
    }

    const json stored = product.product().preferences().document("ai");
    ASSERT_EQ(stored["connections"].size(), 1U);
    EXPECT_EQ(stored["connections"][0]["modelId"], "fake-small");
    EXPECT_EQ(stored["connections"][0]["displayName"], "Local small");
    EXPECT_EQ(stored["connections"][0]["address"], provider.address());
    EXPECT_EQ(stored["connections"][0]["extraParameters"], "{\"options.num_ctx\": 4096}");
    EXPECT_EQ(stored["connections"][0]["parameters"]["temperature"], 0.25);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { const json rows = properties(product, nodes(product, "table", section).front(), section)["rows"]; return rows.size() == 1U && rows[0]["cells"][0] == "Local small"; }));
    // clang-format on
    EXPECT_EQ(properties(product, nodes(product, "combo", section).front(), section)["value"], "ollama/fake-small");

    // Removing the connection asks first.
    emit(product, section, "table", 0, "select", {{"id", "ollama/fake-small"}});
    product.frame();
    click(product, "ai.connection.remove", section);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().shell().dialogs().active(); }));
    // clang-format on
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().preferences().document("ai")["connections"].empty(); }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// An agent is written from a template, refused while its prompt names a tag nobody declares, and named after its title when no identifier is given.
TEST_F(AiPluginTest, WritesAgentsFromTemplates) {
    FakeProvider provider;
    configure(provider);
    HeadlessProduct product(data());
    boot(product);
    createWorkspace(product, "Releases");
    createAgentTask(product, "Plan", "Plan the release");
    EXPECT_EQ(product.query("SELECT agent_id FROM ai__tasks")[0]["agent_id"], "builder");
    const std::string section = "settings:ai:agents:general";
    openSettings(product, "System prompt", section);
    EXPECT_EQ(properties(product, nodes(product, "table", section).front(), section)["rows"][0]["id"], "builder");

    click(product, "ai.agent.add", section);
    std::string form;
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { form = dialog(product); return !form.empty(); }));
    // clang-format on
    emit(product, form, "textField", 0, "change", {{"value", "Release Manager"}});
    emit(product, form, "textArea", 0, "change", {{"value", "Ship {{NOPE}}"}});
    product.frame();
    ASSERT_TRUE(product.answerDialog("save"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(form)).showsKey("ai.validation.agent-tag"); }));
    // clang-format on

    click(product, "ai.agent.insert-template", form);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return properties(product, nodes(product, "textArea", form).front(), form)["value"].get<std::string>().find("{{AGENT_NAME}}") != std::string::npos; }));
    // clang-format on
    ASSERT_TRUE(product.answerDialog("save"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().preferences().document("ai")["agents"].size() == 2U; }));
    // clang-format on
    const json saved = product.product().preferences().document("ai")["agents"][1];
    EXPECT_EQ(saved["id"], "release-manager");
    EXPECT_EQ(saved["connectionKey"], "ollama/fake-large");
    EXPECT_EQ(saved["maximumIterations"], 8);

    // An edited agent keeps its place, and the tasks of the identifier it gave up fail until another agent is chosen.
    emit(product, section, "table", 0, "select", {{"id", "builder"}});
    product.frame();
    click(product, "ai.agent.edit", section);
    std::string editor;
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { editor = dialog(product); return !editor.empty(); }));
    // clang-format on
    emit(product, editor, "textField", 1, "change", {{"value", "planner"}});
    product.frame();
    ASSERT_TRUE(product.answerDialog("save"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().preferences().document("ai")["agents"][0]["id"] == "planner"; }));
    // clang-format on
    EXPECT_EQ(product.product().preferences().document("ai")["agents"][1]["id"], "release-manager");
    ASSERT_TRUE(product.navigate("ai:tasks").hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(view)).showsKey("ai.task.last-error"); }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// The tools an MCP server publishes join the catalog every agent receives, and a call the model makes to one of them is answered by that server.
TEST_F(AiPluginTest, ReachesTheToolsOfAnMcpServer) {
    FakeProvider provider;
    const json server = {{"id", "notes"}, {"transport", "stdio"}, {"command", "notes-server"}, {"arguments", json::array({"--stdio"})}, {"workdir", ""}, {"url", ""}, {"apiKey", ""}, {"roots", json::array()}, {"samplingEnabled", false}, {"samplingMaximumTokens", 4096}};
    configure(provider, 8, json::array({server}));
    provider.script(FakeProvider::calls(json::array({{{"id", "call-1"}, {"name", "mcp_notes_lookup"}, {"arguments", {{"topic", "release"}}}}})));
    provider.script(FakeProvider::answer("Found it."));

    HeadlessProduct product(data());
    product.processes().executables["notes-server"] = RootedPath::of("opt/tools/notes-server");
    serveMcp(product);
    boot(product);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return received("tools/list").size() == 1U; }));
    // clang-format on
    product.settle(std::chrono::milliseconds(100));
    ASSERT_EQ(product.processes().launches.size(), 1U);
    EXPECT_EQ(product.processes().launches[0].program, RootedPath::of("opt/tools/notes-server"));
    EXPECT_EQ(product.processes().launches[0].arguments, std::vector<std::string>{"--stdio"});
    EXPECT_EQ(received("notifications/initialized").size(), 1U);

    createWorkspace(product, "Notes");
    createAgentTask(product, "Look up", "Find the release note.");
    const std::string id = taskId(product, "Look up");
    cardAction(product, "Look up", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded"; }));
    // clang-format on

    const auto requests = provider.requests();
    ASSERT_EQ(requests.size(), 2U);
    // clang-format off
    EXPECT_TRUE(std::ranges::any_of(requests[0]["tools"], [](const json& tool) { return tool["function"]["name"] == "mcp_notes_lookup"; }));
    // clang-format on
    ASSERT_EQ(received("tools/call").size(), 1U);
    EXPECT_EQ(received("tools/call")[0]["params"]["name"], "lookup");
    EXPECT_NE(requests[1]["messages"][3]["content"].get<std::string>().find("Note about release"), std::string::npos);

    // The call reads as the name of the tool over the description its server gave.
    cardAction(product, "Look up", "ai.task.chat");
    std::string surface;
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { surface = dialog(product); return !surface.empty() && SurfaceReader(product.declared(surface)).showsText("Look up a note"); }));
    // clang-format on
    product.press(ImGuiKey_Escape);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !SurfaceReader(product.declared(surface)).mounted(); }));
    // clang-format on

    const std::string section = "settings:ai:tools:mcp";
    openSettings(product, "Transport", section);
    const json rows = properties(product, nodes(product, "table", section).front(), section)["rows"];
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0]["cells"][3], "2");
    product.stop();
    EXPECT_TRUE(product.processes().stopped[0]);
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A command line agent is the program its provider names, run in the working directory with the conversation as its prompt, and what it prints is its answer.
TEST_F(AiPluginTest, RunsACommandLineAgent) {
    const json connection = {{"providerId", "codex-cli"}, {"modelId", "gpt-5.4"}, {"displayName", ""}, {"apiKey", ""}, {"address", ""}, {"parameters", json::object()}, {"extraParameters", ""}};
    store({{"connections", json::array({connection})}, {"agents", json::array({agent("codex-cli/gpt-5.4", 4)})}});
    HeadlessProduct product(data());
    product.processes().executables["codex"] = RootedPath::of("opt/tools/codex");
    boot(product);
    createWorkspace(product, "Code");
    createAgentTask(product, "Refactor", "Tidy the notes.");
    const std::string id = taskId(product, "Refactor");
    cardAction(product, "Refactor", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.processes().launches.size() == 1U; }));
    // clang-format on

    const auto& launch = product.processes().launches[0];
    EXPECT_EQ(launch.program, RootedPath::of("opt/tools/codex"));
    EXPECT_EQ(launch.directory, root());
    // clang-format off
    EXPECT_TRUE(std::ranges::find(launch.cleared, "OPENAI_API_KEY") != launch.cleared.end());
    // clang-format on
    ASSERT_EQ(launch.arguments.size(), 10U);
    EXPECT_EQ(launch.arguments[2], "gpt-5.4");
    EXPECT_EQ(launch.arguments[8], root().generic_string());
    EXPECT_EQ(launch.arguments[9], "-");

    // The conversation reaches the program on its input, which no limit of a command line bounds.
    ASSERT_TRUE(launch.input.has_value());
    EXPECT_NE(launch.input->find("## User\n\nYou are Builder"), std::string::npos);
    EXPECT_NE(launch.input->find("## User\n\nTidy the notes."), std::string::npos);

    product.processes().events[0].output(process::ProcessStream::Output, "Tidied three notes.\n");
    product.processes().events[0].exited({0, false});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded"; }));
    // clang-format on
    const auto stored = product.query("SELECT role, content FROM ai__messages WHERE task_id = ? ORDER BY sequence", {id});
    ASSERT_EQ(stored.size(), 2U);
    EXPECT_EQ(stored[1]["content"], "Tidied three notes.");
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A run stops on the last iteration it was allowed, on a tool called the same way again and again, on an answer cut by its budget and when the reader stops it.
TEST_F(AiPluginTest, RecordsWhyAnAgentStopped) {
    FakeProvider provider;
    configure(provider, 1);
    const json repeated = json::array({{{"id", "call-1"}, {"name", "describe_task"}, {"arguments", json::object()}}});
    provider.script(FakeProvider::calls(repeated));

    HeadlessProduct product(data());
    boot(product);
    createWorkspace(product, "Limits");
    createAgentTask(product, "Loop", "Keep going.");
    const std::string id = taskId(product, "Loop");
    // clang-format off
    const auto stopReason = [&]() { return product.query("SELECT stop_reason FROM ai__executions WHERE task_id = ? AND status <> 'running' ORDER BY started_at_utc DESC, id DESC", {id}); };
    // clang-format on

    cardAction(product, "Loop", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return stopReason().size() == 1U; }));
    // clang-format on
    EXPECT_EQ(stopReason()[0]["stop_reason"], "iteration-limit");
    const json last = provider.requests()[0]["messages"].back();
    EXPECT_EQ(last["role"], "user");
    EXPECT_EQ(last["content"], "This is the last turn of this run. Answer now with what you already have and do not call another tool.");
    product.stop();
}

// A model calling one tool the same way again and again is stopped, and a running agent stopped by the reader ends the program its tool started.
TEST_F(AiPluginTest, StopsARepeatingOrStoppedAgent) {
    FakeProvider provider;
    configure(provider);
    const json repeated = json::array({{{"id", "call-1"}, {"name", "describe_task"}, {"arguments", json::object()}}});

    for (int turn = 0; turn < 4; ++turn) {
        provider.script(FakeProvider::calls(repeated));
    }

    provider.script(FakeProvider::calls(json::array({{{"id", "call-2"}, {"name", "run_command"}, {"arguments", {{"command", "sleep 100"}}}}})));

    HeadlessProduct product(data());
    boot(product);
    createWorkspace(product, "Limits");
    createAgentTask(product, "Loop", "Keep going.");
    const std::string id = taskId(product, "Loop");
    // clang-format off
    const auto reasons = [&]() { return product.query("SELECT stop_reason FROM ai__executions WHERE task_id = ? AND status <> 'running' ORDER BY started_at_utc, id", {id}); };
    // clang-format on

    cardAction(product, "Loop", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return reasons().size() == 1U; }));
    // clang-format on
    EXPECT_EQ(reasons()[0]["stop_reason"], "tool-repetition");
    EXPECT_EQ(provider.requests().size(), 4U);
    EXPECT_TRUE(SurfaceReader(product.declared(view)).showsKey("ai.stop-reason.tool-repetition"));

    cardAction(product, "Loop", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.processes().launches.size() == 1U; }));
    // clang-format on
    EXPECT_EQ(product.processes().launches[0].arguments, shellArguments("sleep 100"));
    cardAction(product, "Loop", "ai.task.stop");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return reasons().size() == 2U && product.processes().stopped[0]; }));
    // clang-format on
    EXPECT_EQ(reasons()[1]["stop_reason"], "cancelled");
    // The column of the task is written after the run it closes, so it is awaited rather than read at once.
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT column_name FROM ai__tasks")[0]["column_name"] == "todo"; }));
    // clang-format on

    // The call the stop interrupted is answered as interrupted when the task runs again, so every protocol still reads the conversation.
    provider.script(FakeProvider::answer("Done."));
    cardAction(product, "Loop", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return reasons().size() == 3U; }));
    // clang-format on
    const json resumed = provider.requests().back()["messages"];
    // clang-format off
    const auto interrupted = std::ranges::find_if(resumed, [](const json& message) { return message.value("tool_call_id", "") == "call-2"; });
    // clang-format on
    ASSERT_NE(interrupted, resumed.end());
    EXPECT_EQ((*interrupted)["content"], "The call to \"run_command\" was interrupted before it answered, so its result is unknown.");
    EXPECT_EQ((*std::next(interrupted))["role"], "user");
    EXPECT_EQ(reasons()[2]["stop_reason"], "answered");
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// Files attached to a message travel with it the way the model reads them: a text becomes its text, and an image or a PDF the model cannot read becomes a note that says so.
TEST_F(AiPluginTest, SendsTheAttachmentsOfAMessageTheWayTheModelReadsThem) {
    FakeProvider provider;
    configure(provider);
    provider.script(FakeProvider::answer("Hello."));
    provider.script(FakeProvider::answer("Read them."));
    std::ofstream(root() / "shot.png", std::ios::binary) << "PNGDATA";
    std::ofstream(root() / "brief.pdf", std::ios::binary) << "%PDF-1.4 /Type /Page >>";
    std::ofstream(root() / "notes.md", std::ios::binary) << "# Notes\nKeep it short.";
    std::ofstream(root() / "clip.exe", std::ios::binary) << "MZ";

    HeadlessProduct product(data());
    boot(product);
    createWorkspace(product, "Files");
    createAgentTask(product, "Read", "Say hello.");
    const std::string id = taskId(product, "Read");
    cardAction(product, "Read", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded"; }));
    // clang-format on
    cardAction(product, "Read", "ai.task.chat");
    std::string surface;
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { surface = dialog(product); return !surface.empty() && SurfaceReader(product.declared(surface)).showsText("Hello."); }));
    // clang-format on

    // A file the agents cannot read is refused by its name, and the others wait beside the composer.
    product.dialogs().paths = {(root() / "shot.png").generic_string(), (root() / "brief.pdf").generic_string(), (root() / "notes.md").generic_string(), (root() / "clip.exe").generic_string()};
    clickTooltip(product, surface, "ai.conversation.attach");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(surface)).showsText("notes.md"); }));
    // clang-format on
    EXPECT_TRUE(product.product().shell().toasts().showing("AI", "The file \"clip.exe\" is not an image, a PDF, a text or an audio the agents read."));

    emit(product, surface, "textArea", 0, "change", {{"value", "Read these."}});
    emit(product, surface, "textArea", 0, "submit", json::object());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(surface)).showsText("Read them."); }));
    // clang-format on
    const auto requests = provider.requests();
    ASSERT_EQ(requests.size(), 2U);
    EXPECT_EQ(requests[1]["messages"].back()["content"], "Read these.\n\nAn image was attached here, but the selected model does not read images.\n\nThe document \"brief.pdf\" was attached here, but the selected model does not read PDF documents.\n\nThe attached document \"notes.md\" says:\n\n# Notes\nKeep it short.");

    // The message keeps its files, the conversation names them and the run says why two of them went unread.
    const json parts = json::parse(product.query("SELECT parts_json FROM ai__messages WHERE content = 'Read these.'").at(0)["parts_json"].get<std::string>());
    ASSERT_EQ(parts.size(), 3U);
    EXPECT_EQ(parts[0]["type"], "image");
    EXPECT_EQ(parts[1]["name"], "brief.pdf");
    EXPECT_EQ(parts[2]["mediaType"], "text/markdown");
    EXPECT_TRUE(SurfaceReader(product.declared(surface)).showsKey("ai.message.image-described"));
    EXPECT_TRUE(SurfaceReader(product.declared(surface)).showsText("brief.pdf"));
    const auto adjusted = product.query("SELECT detail FROM ai__logs WHERE kind = 'adjusted' ORDER BY sequence");
    ASSERT_EQ(adjusted.size(), 2U);
    EXPECT_EQ(adjusted[0]["detail"], "Images were replaced by a note because the model does not read images");

    // Every kind of entry the log of a run holds is named in the language of the reader.
    for (const auto& row : product.query("SELECT DISTINCT kind FROM ai__logs")) {
        EXPECT_TRUE(product.product().localization().contains("ai.log-kind." + row["kind"].get<std::string>())) << row["kind"];
    }

    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// Another plugin sends a conversation with typed parts and receives, as plain data, the answer, its calls and what the model forced on the conversation, or the code of the rule it broke.
// A command-line connection is refused, since a command-line agent runs a task of its own rather than answering a completion.
TEST_F(AiPluginTest, AnswersAConversationAnotherPluginSends) {
    FakeProvider provider;
    const json cli = {{"providerId", "codex-cli"}, {"modelId", "gpt-5.4"}, {"displayName", ""}, {"apiKey", ""}, {"address", ""}, {"parameters", json::object()}, {"extraParameters", ""}};
    store({{"connections", json::array({localConnection(provider), cli})}, {"defaultConnectionKey", "ollama/fake-large"}, {"agents", json::array({agent("ollama/fake-large", 8)})}, {"mcpServers", json::array()}});
    provider.script(FakeProvider::answer("I see a note."));
    provider.script(FakeProvider::calls(json::array({{{"id", "c1"}, {"name", "lookup"}, {"arguments", {{"topic", "release"}, {"tags", json::array({"a", "b"})}, {"note", nullptr}}}}})));

    HeadlessProduct product(data());
    boot(product);
    const auto loaded = product.product().runtime().loadString(R"(
        local bridge = require("workpane.bridge")
        local task = require("workpane.task")
        local codec = require("json")
        local workpane = require("workpane.api").create("logs", "", { version = "", debug = false, platform = "", architecture = "", paths = { data = "" }, languages = {}, themes = {}, icons = {}, colors = {} })

        task.run("logs", "suite", function()
            local function complete(payload)
                local answer, failure = workpane.capabilities.request("ai.chat.complete", payload):await()
                return answer or { code = failure.code }
            end

            local answered = complete({ messages = { { role = "system", content = "Be brief." }, { role = "user", content = { { type = "text", text = "What is this?" }, { type = "image", mediaType = "image/png", data = "PNGDATA" } } } }, maximumTokens = 64 })
            local called = complete({ connection = "ollama/fake-large", messages = { { role = "user", content = "Look it up." } }, tools = { { name = "lookup", description = "Look up a note", parameters = { type = "object", properties = { topic = { type = "string" } } } } } })
            local codes = {
                complete({ messages = {} }).code,
                complete({ messages = { { role = "user", content = "hi" } }, colour = "red" }).code,
                complete({ connection = "nobody/none", messages = { { role = "user", content = "hi" } } }).code,
                complete({ messages = { { role = "user", content = "hi" } }, maximumTokens = 0 }).code,
                complete({ messages = { { role = "user", content = "hi" } }, tools = { { name = "bad name", description = "x", parameters = { type = "object", properties = {} } } } }).code,
                complete({ connection = "codex-cli/gpt-5.4", messages = { { role = "user", content = "hi" } } }).code,
            }

            local summary = { answered.content, answered.connection, answered.model, table.concat(answered.adjustments, ","), answered.finishReason, called.toolCalls[1].name, called.toolCalls[1].arguments.topic, #called.toolCalls[1].arguments.tags, tostring(called.toolCalls[1].arguments.note), called.finishReason, table.concat(codes, ",") }
            bridge.call("workpane_log", { plugin = "logs", level = "info", category = "suite", message = "completion:" .. table.concat(summary, "|"), details = {} })
        end)
    )",
                                                               "capability");
    ASSERT_TRUE(loaded.hasValue()) << loaded.error().message;
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.query("SELECT message FROM logs__entries WHERE message LIKE 'completion:%'").empty(); }));
    // clang-format on
    EXPECT_EQ(product.query("SELECT message FROM logs__entries WHERE message LIKE 'completion:%'").at(0)["message"], "completion:I see a note.|ollama/fake-large|fake-large|image-omitted|stop|lookup|release|2|nil|tool_calls|ai_conversation_empty,ai_completion_invalid,ai_connection_unknown,ai_completion_invalid,ai_completion_invalid,ai_completion_command_line");

    const auto requests = provider.requests();
    ASSERT_EQ(requests.size(), 2U);
    EXPECT_EQ(requests[0]["messages"][0], json({{"role", "system"}, {"content", "Be brief."}}));
    EXPECT_EQ(requests[0]["messages"][1]["content"], "What is this?\n\nAn image was attached here, but the selected model does not read images.");
    EXPECT_EQ(requests[0]["max_tokens"], 64);
    EXPECT_EQ(requests[1]["tools"][0]["function"]["name"], "lookup");
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// Tables an earlier build wrote, such as schedules without the zone they are kept in, refuse the plugin at the start and tell the reader where the data is.
TEST_F(AiPluginTest, RefusesToStartOverTablesAnEarlierBuildWrote) {
    {
        HeadlessProduct product(data());
        boot(product);
        product.stop();
    }

    {
        auto database = openDatabase();
        ASSERT_TRUE(database.run("ALTER TABLE ai__schedules DROP COLUMN time_zone").hasValue());
    }

    HeadlessProduct product(data());
    ASSERT_TRUE(product.boot().hasValue());
    EXPECT_FALSE(product.navigate("ai:tasks").hasValue());
    const std::string file = (data() / persistence::DatabaseBootstrap::databaseName).generic_string();
    // clang-format off
    EXPECT_TRUE(product.frameUntil([&]() { return product.product().shell().toasts().showing("A plugin could not be loaded", "The stored data of \"AI\" could not be read, so it was not loaded. Its data is kept in \"" + file + "\""); }));
    // clang-format on
    product.stop();

    const auto stored = errors(product);
    ASSERT_EQ(stored.size(), 1U);
    const json details = json::parse(stored[0]["details_json"].get<std::string>());
    EXPECT_EQ(details["code"], "database_schema_mismatch");
    EXPECT_EQ(details["detail"], "ai__schedules");
}

// Closing a workspace that is not the active one keeps the workspace the reader was on.
TEST_F(AiPluginTest, KeepsTheActiveWorkspaceWhenAnotherOneCloses) {
    HeadlessProduct product(data());
    boot(product);
    createWorkspace(product, "First");
    createWorkspace(product, "Second");
    createWorkspace(product, "Third");
    // clang-format off
    const auto workspace = [&](std::string_view name) { return product.query("SELECT id FROM ai__workspaces WHERE name = ?", {std::string(name)})[0]["id"].get<std::string>(); };
    const auto active = [&]() { const auto rows = product.query("SELECT name FROM ai__workspaces WHERE active = 1"); return rows.empty() ? std::string() : rows[0]["name"].get<std::string>(); };
    // clang-format on
    emit(product, view, "tabs", 0, "select", {{"id", workspace("First")}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return active() == "First"; }));
    // clang-format on

    emit(product, view, "tabs", 0, "close", {{"id", workspace("Second")}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([]() { return ImGui::GetTopMostPopupModal() != nullptr; }));
    // clang-format on
    product.frame();
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT id FROM ai__workspaces").size() == 2U; }));
    // clang-format on

    EXPECT_EQ(active(), "First");
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A task started twice at once records its prompt and runs once, since the second start finds it already starting.
TEST_F(AiPluginTest, StartsATaskOnceWhenItIsStartedTwiceAtOnce) {
    FakeProvider provider;
    configure(provider);
    provider.script(FakeProvider::answer("Once."));
    HeadlessProduct product(data());
    boot(product);
    createWorkspace(product, "Twice");
    createAgentTask(product, "Double", "Run once.");
    const std::string id = taskId(product, "Double");
    cardAction(product, "Double", "ai.task.start");
    cardAction(product, "Double", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded"; }));
    // clang-format on
    product.settle(std::chrono::milliseconds(300));

    EXPECT_EQ(product.query("SELECT id FROM ai__messages WHERE task_id = ? AND content = 'Run once.'", {id}).size(), 1U);
    EXPECT_EQ(product.query("SELECT id FROM ai__executions WHERE task_id = ?", {id}).size(), 1U);
    EXPECT_EQ(provider.requests().size(), 1U);
    product.stop();
}

// An idle product without a schedule keeps no coroutine of the AI plugin alive, so the frame loop sleeps instead of waking for a scheduler with nothing to do.
TEST_F(AiPluginTest, LeavesTheFrameLoopAsleepWithoutSchedules) {
    HeadlessProduct product(data());
    boot(product);
    product.settle(std::chrono::milliseconds(1500));

    product.product().pollScripts();
    EXPECT_TRUE(std::isinf(product.product().scriptIdleSeconds()));
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A schedule is written with its task, refused in the past or with a broken expression, run when it falls due, even after a restart, and removed on request.
TEST_F(AiPluginTest, RunsSchedulesThatFallDue) {
    {
        HeadlessProduct product(data());
        boot(product);
        createWorkspace(product, "Nightly");
        const std::string form = openTaskForm(product);
        emit(product, form, "combo", 0, "change", {{"value", "command"}});
        emit(product, form, "textField", 0, "change", {{"value", "Backup"}});
        emit(product, form, "textField", 3, "change", {{"value", root().generic_string()}});
        emit(product, form, "textField", 4, "change", {{"value", "backup --all"}});
        emit(product, form, "combo", 2, "change", {{"value", "once"}});
        emit(product, form, "dateTimeField", 0, "change", {{"value", "2020-01-01 10:00"}});
        product.frame();
        ASSERT_TRUE(product.answerDialog("save"));
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(form)).showsKey("ai.validation.schedule-past"); }));
        // clang-format on

        // The date field took the keyboard, and hiding it for a cron expression gives the keyboard back, so saving from the keyboard still reaches the button.
        emit(product, form, "combo", 2, "change", {{"value", "cron"}});
        emit(product, form, "textField", 5, "change", {{"value", "61 * * * *"}});
        product.frame();
        ASSERT_TRUE(product.answerDialog("save"));
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(form)).showsKey("ai.validation.cron"); }));
        // clang-format on
        emit(product, form, "combo", 2, "change", {{"value", "interval"}});
        emit(product, form, "numberField", 1, "change", {{"value", 5.0}});
        product.frame();
        ASSERT_TRUE(product.answerDialog("save"));
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT task_id FROM ai__schedules").size() == 1U; }));
        // clang-format on
        const auto schedule = product.query("SELECT schedule_kind, interval_seconds, enabled, last_triggered_at_utc FROM ai__schedules");
        EXPECT_EQ(schedule[0]["schedule_kind"], "interval");
        EXPECT_EQ(schedule[0]["interval_seconds"], 300);
        EXPECT_EQ(schedule[0]["enabled"], 1);
        EXPECT_EQ(schedule[0]["last_triggered_at_utc"], "");
        EXPECT_TRUE(SurfaceReader(product.declared(view)).showsKey("ai.task.scheduled"));
        product.stop();
        EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    }

    {
        auto database = openDatabase();
        ASSERT_TRUE(database.run("UPDATE ai__schedules SET next_run_at_utc = '2020-01-01T00:00:00.000Z'").hasValue());
    }

    std::string id;
    {
        HeadlessProduct product(data());
        boot(product);
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.processes().launches.size() == 1U; }));
        // clang-format on
        EXPECT_EQ(product.processes().launches[0].arguments, shellArguments("backup --all"));
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT last_triggered_at_utc FROM ai__schedules")[0]["last_triggered_at_utc"] != ""; }));
        // clang-format on
        EXPECT_GT(product.query("SELECT next_run_at_utc FROM ai__schedules")[0]["next_run_at_utc"].get<std::string>(), "2026-01-01");
        product.processes().events[0].exited({0, false});
        id = taskId(product, "Backup");
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded"; }));
        // clang-format on

        // A start by hand keeps the schedule the task would otherwise wait for.
        cardAction(product, "Backup", "ai.task.start");
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.processes().launches.size() == 2U; }));
        // clang-format on
        product.processes().events[1].exited({0, false});
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT id FROM ai__executions WHERE status = 'succeeded'").size() == 2U; }));
        // clang-format on
        EXPECT_EQ(product.query("SELECT task_id FROM ai__schedules WHERE enabled = 1").size(), 1U);
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT id FROM ai__tasks WHERE column_name = 'doing'").empty() && product.query("SELECT task_id FROM ai__queue").empty(); }));
        // clang-format on
        product.stop();
        EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
    }

    // A due schedule whose start is refused, here because its agent is gone, moves to its next occurrence and is reported once.
    {
        auto database = openDatabase();
        ASSERT_TRUE(database.run("UPDATE ai__tasks SET execution_kind = 'agent', agent_id = 'gone', prompt = 'Back up'").hasValue());
        ASSERT_TRUE(database.run("UPDATE ai__schedules SET next_run_at_utc = '2020-01-01T00:00:00.000Z'").hasValue());
    }

    {
        HeadlessProduct product(data());
        boot(product);
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT next_run_at_utc FROM ai__schedules")[0]["next_run_at_utc"].get<std::string>() > "2026-01-01"; }));
        // clang-format on
        product.settle(std::chrono::milliseconds(2500));
        EXPECT_EQ(errors(product).size(), 1U);
        EXPECT_EQ(product.query("SELECT task_id FROM ai__schedules WHERE enabled = 1").size(), 1U);
        product.stop();
    }

    // A due schedule that cannot be advanced, here a cron expression that never falls, is stored paused without a next occurrence, so the plugin still starts after it.
    {
        auto database = openDatabase();
        ASSERT_TRUE(database.run("DELETE FROM logs__entries").hasValue());
        ASSERT_TRUE(database.run("UPDATE ai__tasks SET execution_kind = 'command', agent_id = ''").hasValue());
        ASSERT_TRUE(database.run("UPDATE ai__schedules SET schedule_kind = 'cron', interval_seconds = 0, cron_expression = '0 0 30 2 *', time_zone = 'UTC', next_run_at_utc = '2020-01-01T00:00:00.000Z'").hasValue());
    }

    {
        HeadlessProduct product(data());
        boot(product);
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT enabled FROM ai__schedules")[0]["enabled"] == 0; }));
        // clang-format on
        product.settle(std::chrono::milliseconds(300));
        EXPECT_EQ(product.query("SELECT next_run_at_utc FROM ai__schedules")[0]["next_run_at_utc"], "");
        EXPECT_EQ(errors(product).size(), 1U);
        product.stop();
    }

    // The task becomes a command again, so the schedule can be removed from its card, which the plugin that started over the paused schedule offers.
    {
        auto database = openDatabase();
        ASSERT_TRUE(database.run("DELETE FROM logs__entries").hasValue());
        ASSERT_TRUE(database.run("UPDATE ai__tasks SET execution_kind = 'command', agent_id = ''").hasValue());
    }

    HeadlessProduct product(data());
    boot(product);
    cardAction(product, "Backup", "ai.task.schedule-remove");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().shell().dialogs().active(); }));
    // clang-format on
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT task_id FROM ai__schedules").empty(); }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A card opens its working directory in the Code Editor and offers it to the Web Server and opens its form on a double click before it ever ran.
TEST_F(AiPluginTest, ReachesOtherPluginsFromACard) {
    HeadlessProduct product(data());
    boot(product);
    createWorkspace(product, "Site");
    createCommandTask(product, "Publish", "make site");

    // clang-format off
    const auto card = [&]() { for (const auto node : nodes(product, "card")) { if (properties(product, node).value("drag", json::object()).value("label", "") == "Publish") { return node; } } return ui::NodeId{}; };
    // clang-format on
    product.emit(view, card(), "activate", json::object());
    std::string form;
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { form = dialog(product); return !form.empty() && properties(product, nodes(product, "textField", form).front(), form)["value"] == "Publish"; }));
    // clang-format on
    ASSERT_TRUE(product.answerDialog("cancel"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !SurfaceReader(product.declared(form)).mounted(); }));
    // clang-format on

    // The zoom keys belong to no destination, since the chat they would size lives in a dialog.
    product.press(HeadlessProduct::command() | ImGuiKey_Equal);
    product.settle(std::chrono::milliseconds(100));
    EXPECT_EQ(product.product().preferences().document("ai").value("chatFontSize", 11), 11);

    cardMenu(product, "Publish", "workspace.folder.serve");
    std::string serve;
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { serve = dialog(product, "web-server"); return !serve.empty(); }));
    // clang-format on
    EXPECT_TRUE(SurfaceReader(product.declared(serve)).showsText(root().generic_string()));
    ASSERT_TRUE(product.answerDialog("close"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.product().shell().dialogs().active(); }));
    // clang-format on

    cardMenu(product, "Publish", "workspace.folder.open");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT root_path FROM code_editor__workspaces") == std::vector<json>{{{"root_path", root().generic_string()}}}; }));
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared("view:code-editor:editor")).mounted(); }));
    // clang-format on

    // The menu offers only the actions a running plugin answers, so turning the Code Editor off takes its item away.
    const std::string plugins = "settings:workpane:plugins:installed";
    openSettings(product, "plugin", plugins);
    const SurfaceReader reader(product.declared(plugins));
    const auto rows = reader.nodes("settingsRow");
    const auto toggles = reader.nodes("toggle");

    for (std::size_t index = 0; index < rows.size() && index < toggles.size(); ++index) {
        if (reader.properties(rows[index])["label"].value("key", "") == "code-editor.plugin.title") {
            product.emit(plugins, toggles[index], "change", {{"checked", false}});
        }
    }

    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().plugins().find("code-editor") == nullptr; }));
    // clang-format on
    ASSERT_TRUE(product.navigate("ai:tasks").hasValue());
    // clang-format off
    const auto offered = [&]() { const auto menu = cardNode(product, "menuButton", "Publish", "ai.task.workdir"); json ids = json::array(); for (const auto& item : menu.has_value() ? properties(product, *menu)["items"] : json::array()) { ids.push_back(item["id"]); } return ids; };
    ASSERT_TRUE(product.frameUntil([&]() { return offered() == json::array({"workspace.folder.serve"}); }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// The surface of a task lists its runs with their logs and output, opens an exchanged payload whole and clears the conversation on request.
TEST_F(AiPluginTest, ShowsTheRunsOfATask) {
    FakeProvider provider;
    configure(provider);
    provider.script(FakeProvider::answer("All **done**."));

    HeadlessProduct product(data());
    boot(product);
    createWorkspace(product, "Runs");
    createAgentTask(product, "Report", "Write the report.");
    const std::string id = taskId(product, "Report");
    cardAction(product, "Report", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded"; }));
    // clang-format on

    cardAction(product, "Report", "ai.task.info");
    std::string surface;
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { surface = dialog(product); return !surface.empty() && !nodes(product, "table", surface).empty() && !properties(product, nodes(product, "table", surface).front(), surface)["rows"].empty(); }));
    // clang-format on
    const json runs = properties(product, nodes(product, "table", surface)[0], surface)["rows"];
    ASSERT_EQ(runs.size(), 1U);
    EXPECT_EQ(runs[0]["cells"][1]["text"]["key"], "ai.status.succeeded");
    EXPECT_EQ(runs[0]["cells"][4], "stop");

    // clang-format off
    const auto markdown = [&]() { for (const auto node : nodes(product, "markdown", surface)) { if (properties(product, node, surface).value("text", json()) == "All **done**.") { return true; } } return false; };
    ASSERT_TRUE(product.frameUntil([&]() { return markdown(); }));
    // clang-format on

    const json logs = properties(product, nodes(product, "table", surface)[1], surface)["rows"];
    // clang-format off
    const auto sent = std::ranges::find_if(logs, [](const json& row) { return row["cells"][2]["text"]["key"] == "ai.log-kind.request-sent"; });
    const auto received = std::ranges::find_if(logs, [](const json& row) { return row["cells"][2]["text"]["key"] == "ai.log-kind.response-received"; });
    // clang-format on

    // A request is logged by where it went, its size and the messages it carried, never with the conversation it sent.
    ASSERT_NE(sent, logs.end());
    const std::string request = (*sent)["cells"][3];
    EXPECT_NE(request.find("/chat/completions"), std::string::npos);
    EXPECT_NE(request.find(" bytes and 2 messages sent to "), std::string::npos);
    EXPECT_EQ(request.find("Write the report."), std::string::npos);
    EXPECT_FALSE(sent->contains("actions"));

    ASSERT_NE(received, logs.end());
    emit(product, surface, "table", 1, "action", {{"id", (*received)["id"]}, {"action", "payload"}});
    std::string viewer;
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { viewer = dialog(product); return viewer != surface && !viewer.empty(); }));
    // clang-format on
    EXPECT_EQ(properties(product, nodes(product, "codeEditor", viewer).front(), viewer)["value"], "All **done**.");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([]() { return GImGui->OpenPopupStack.Size == 2; }));
    // clang-format on
    product.press(ImGuiKey_Escape);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return dialog(product) == surface; }));
    // clang-format on

    click(product, "ai.conversation.reset", surface);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([]() { return GImGui->OpenPopupStack.Size == 2; }));
    // clang-format on
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT id FROM ai__messages").empty(); }));
    // clang-format on

    // The close key of the platform closes the surface like the window it stands for.
    product.press(HeadlessProduct::command() | ImGuiKey_W);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !SurfaceReader(product.declared(surface)).mounted(); }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << ::testing::PrintToString(errors(product));
}

// A long conversation opens on its newest page and reads the older ones as the reader scrolls to its top.
TEST_F(AiPluginTest, PagesThroughALongConversation) {
    FakeProvider provider;
    configure(provider);
    std::string id;

    {
        HeadlessProduct product(data());
        boot(product);
        createWorkspace(product, "Long");
        createAgentTask(product, "Chatty", "Talk a lot.");
        id = taskId(product, "Chatty");
        product.stop();
    }

    {
        auto database = openDatabase();

        for (int sequence = 1; sequence <= 150; ++sequence) {
            const std::vector<json> bindings = {"m" + std::to_string(sequence), id, sequence, sequence % 2 == 1 ? "user" : "assistant", "message " + std::to_string(sequence), "[]", "", 0, "[]", "2026-01-01T00:00:00.000Z"};
            ASSERT_TRUE(database.run("INSERT INTO ai__messages(id, task_id, sequence, role, content, tool_calls, tool_call_id, summarized_until, parts_json, created_at_utc) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", bindings).hasValue());
        }
    }

    HeadlessProduct product(data());
    boot(product);
    cardAction(product, "Chatty", "ai.task.chat");
    std::string surface;
    // clang-format off
    const auto shows = [&](std::string_view text) { for (const auto node : nodes(product, "markdown", surface)) { if (properties(product, node, surface).value("text", json()) == json(text)) { return true; } } return false; };
    ASSERT_TRUE(product.frameUntil([&]() { surface = dialog(product); return !surface.empty() && shows("message 150"); }));
    // clang-format on
    EXPECT_FALSE(shows("message 1"));
    emit(product, surface, "scroll", 0, "top", json::object());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return shows("message 1"); }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// The conversation of a task keeps in memory only its newest pages or what the reader paged back to, and a message it let go of is read again when the reader scrolls to it.
TEST_F(AiPluginTest, KeepsABoundedConversationInMemory) {
    FakeProvider provider;
    configure(provider);
    provider.script(FakeProvider::answer("Answer."));
    std::string id;

    {
        HeadlessProduct product(data());
        boot(product);
        createWorkspace(product, "Long");
        createAgentTask(product, "Chatty", "Talk a lot.");
        id = taskId(product, "Chatty");
        product.stop();
    }

    {
        auto database = openDatabase();

        for (int sequence = 1; sequence <= 199; ++sequence) {
            const std::vector<json> bindings = {"m" + std::to_string(sequence), id, sequence, sequence % 2 == 1 ? "user" : "assistant", "message " + std::to_string(sequence), "[]", "", 0, "[]", "2026-01-01T00:00:00.000Z"};
            ASSERT_TRUE(database.run("INSERT INTO ai__messages(id, task_id, sequence, role, content, tool_calls, tool_call_id, summarized_until, parts_json, created_at_utc) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", bindings).hasValue());
        }
    }

    HeadlessProduct product(data());
    boot(product);
    cardAction(product, "Chatty", "ai.task.chat");
    std::string surface;
    // clang-format off
    const auto shows = [&](std::string_view text) { for (const auto node : nodes(product, "markdown", surface)) { if (properties(product, node, surface).value("text", json()) == json(text)) { return true; } } return false; };
    ASSERT_TRUE(product.frameUntil([&]() { surface = dialog(product); return !surface.empty() && shows("message 199"); }));
    // clang-format on
    emit(product, surface, "scroll", 0, "top", json::object());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return shows("message 1"); }));
    // clang-format on

    // Two more messages pass the two pages the reader holds, so the oldest one leaves memory while the store keeps it.
    emit(product, surface, "textArea", 0, "change", {{"value", "More."}});
    emit(product, surface, "textArea", 0, "submit", json::object());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded" && product.query("SELECT id FROM ai__messages WHERE task_id = ?", {id}).size() == 201U; }));
    // clang-format on
    product.frame();
    EXPECT_TRUE(shows("Answer."));
    EXPECT_FALSE(shows("message 1"));
    EXPECT_TRUE(shows("message 2"));

    emit(product, surface, "scroll", 0, "top", json::object());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return shows("message 1"); }));
    // clang-format on
    EXPECT_TRUE(shows("Answer."));
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A server is written through its form and connected at once, and search, speech, execution and the limits of each provider are kept as the reader changes them.
TEST_F(AiPluginTest, WritesServersServicesAndLimits) {
    HeadlessProduct product(data());
    product.processes().executables["files-server"] = RootedPath::of("opt/tools/files-server");
    serveMcp(product);
    boot(product);
    const std::string servers = "settings:ai:tools:mcp";
    openSettings(product, "Transport", servers);
    click(product, "ai.mcp.add", servers);
    std::string form;
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { form = dialog(product); return !form.empty(); }));
    // clang-format on

    // The key of a server is shown in plain text only once the reader confirms it.
    const auto secret = nodes(product, "secretField", form).front();
    EXPECT_TRUE(properties(product, secret, form)["confirmReveal"].get<bool>());
    product.emit(form, secret, "reveal-request", json::object());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([]() { return GImGui->OpenPopupStack.Size == 2; }));
    // clang-format on
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return properties(product, secret, form).value("revealed", false) && GImGui->OpenPopupStack.Size == 1; }));
    // clang-format on

    emit(product, form, "textField", 0, "change", {{"value", "Files Server"}});
    product.frame();
    ASSERT_TRUE(product.answerDialog("save"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(form)).showsKey("ai.validation.mcp-identifier"); }));
    // clang-format on
    emit(product, form, "textField", 0, "change", {{"value", "files"}});
    emit(product, form, "textField", 1, "change", {{"value", "files-server"}});
    emit(product, form, "textArea", 0, "change", {{"value", "--root\n /srv/my notes \n"}});

    // A root is a folder named from the root of its disk, so a relative one is refused before anything is saved.
    emit(product, form, "textArea", 1, "change", {{"value", "relative/notes"}});
    product.frame();
    ASSERT_TRUE(product.answerDialog("save"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(form)).showsKey("ai.validation.mcp-root"); }));
    // clang-format on
    emit(product, form, "textArea", 1, "change", {{"value", ""}});
    product.frame();
    ASSERT_TRUE(product.answerDialog("save"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return received("tools/list").size() == 1U; }));
    // clang-format on
    const json server = product.product().preferences().document("ai")["mcpServers"][0];
    EXPECT_EQ(server["id"], "files");
    EXPECT_EQ(server["arguments"], json::array({"--root", "/srv/my notes"}));
    EXPECT_EQ(product.processes().launches[0].program, RootedPath::of("opt/tools/files-server"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { const json rows = properties(product, nodes(product, "table", servers).front(), servers)["rows"]; return rows.size() == 1U && rows[0]["cells"][3] == "2"; }));
    // clang-format on

    emit(product, servers, "table", 0, "select", {{"id", "files"}});
    product.frame();
    click(product, "ai.mcp.remove", servers);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().shell().dialogs().active(); }));
    // clang-format on
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().preferences().document("ai")["mcpServers"].empty() && product.processes().stopped[0]; }));
    // clang-format on

    const std::string search = "settings:ai:tools:search";
    openSettings(product, "Instance", search);
    emit(product, search, "combo", 0, "change", {{"value", "searxng"}});
    emit(product, search, "textField", 0, "blur", {{"value", "http://127.0.0.1:8888"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { const json saved = product.product().preferences().document("ai"); return saved.value("searchProvider", "") == "searxng" && saved.value("searchInstanceUrl", "") == "http://127.0.0.1:8888"; }));
    // clang-format on

    // A key left empty names the variable the service keeps its key in as its placeholder, and is never stored in its place.
    emit(product, search, "combo", 0, "change", {{"value", "brave"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return properties(product, nodes(product, "secretField", search).front(), search).value("placeholder", "") == "{env.BRAVE_API_KEY}"; }));
    // clang-format on
    EXPECT_EQ(product.product().preferences().document("ai").value("searchApiKey", ""), "");

    const std::string speech = "settings:ai:tools:speech";
    openSettings(product, "Voice", speech);
    emit(product, speech, "combo", 0, "change", {{"value", "openai"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return properties(product, nodes(product, "combo", speech)[1], speech).value("visible", true) && properties(product, nodes(product, "combo", speech)[1], speech)["value"] == "alloy"; }));
    // clang-format on
    emit(product, speech, "combo", 1, "change", {{"value", "nova"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { const json saved = product.product().preferences().document("ai"); return saved.value("speechProvider", "") == "openai" && saved.value("speechVoiceId", "") == "nova" && saved.value("speechApiKey", "x") == ""; }));
    // clang-format on
    EXPECT_EQ(properties(product, nodes(product, "secretField", speech).front(), speech).value("placeholder", ""), "{env.OPENAI_API_KEY}");

    const std::string execution = "settings:ai:general:general";
    openSettings(product, "Parallel executions", execution);
    emit(product, execution, "numberField", 1, "change", {{"value", 3.0}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().preferences().document("ai").value("parallelExecutions", 0) == 3; }));
    // clang-format on

    const std::string scope = "settings:ai:providers:selection";
    const std::string limits = "settings:ai:providers:rate-limits";
    openSettings(product, "Settings for", scope);
    emit(product, scope, "combo", 0, "change", {{"value", "ollama"}});
    product.frame();
    openSettings(product, "Maximum requests at the same time", limits);
    emit(product, limits, "numberField", 0, "change", {{"value", 1500.0}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { const json saved = product.product().preferences().document("ai").value("rateLimits", json::array()); return saved.size() == 1U && saved[0]["providerId"] == "ollama" && saved[0]["minimumIntervalMs"] == 1500; }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// Runs beyond the parallel limit wait in the queue, and a queue and a run the product closed on are taken up again when it starts.
TEST_F(AiPluginTest, KeepsTheQueueAcrossARestart) {
    {
        HeadlessProduct product(data());
        boot(product);
        createWorkspace(product, "Queue");
        createCommandTask(product, "First", "first");
        createCommandTask(product, "Second", "second");
        cardAction(product, "First", "ai.task.start");
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.processes().launches.size() == 1U; }));
        // clang-format on
        cardAction(product, "Second", "ai.task.start");
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(view)).showsKey("ai.badge.queued"); }));
        // clang-format on

        // The running card is outlined in the success color and the queued one in the warning color.
        std::map<std::string, std::string> outlines;

        for (const auto card : nodes(product, "card")) {
            const json props = properties(product, card);
            outlines[props["drag"]["label"].get<std::string>()] = props.value("outline", "");
        }

        EXPECT_EQ(outlines["First"], "success");
        EXPECT_EQ(outlines["Second"], "warning");
        // The queue is written on the database thread after the board shows it, so its rows are awaited.
        // clang-format off
        ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT task_id FROM ai__queue").size() == 2U; }));
        // clang-format on
        EXPECT_EQ(product.processes().launches.size(), 1U);
        product.stop();
    }

    HeadlessProduct product(data());
    boot(product);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.processes().launches.size() == 1U; }));
    // clang-format on
    EXPECT_EQ(product.processes().launches[0].arguments.back(), "first");
    const std::string first = taskId(product, "First");
    const std::string second = taskId(product, "Second");
    // clang-format off
    const auto executions = [&]() { return product.query("SELECT status FROM ai__executions WHERE task_id = ? ORDER BY started_at_utc, id", {first}); };
    ASSERT_TRUE(product.frameUntil([&]() { return executions().size() == 2U; }));
    // clang-format on
    const auto interrupted = executions();
    EXPECT_EQ(interrupted[0]["status"], "cancelled");
    product.processes().events[0].exited({0, false});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.processes().launches.size() == 2U; }));
    // clang-format on
    EXPECT_EQ(product.processes().launches[1].arguments.back(), "second");
    product.processes().events[1].exited({0, false});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, first) == "succeeded" && status(product, second) == "succeeded"; }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A message typed while a run is going joins the conversation and is answered in the same run, and removing the agent of a task stops it with an error.
TEST_F(AiPluginTest, TakesMessagesWhileARunGoes) {
    FakeProvider provider;
    configure(provider);
    provider.script(FakeProvider::calls(json::array({{{"id", "call-1"}, {"name", "run_command"}, {"arguments", {{"command", "wait"}}}}})));
    provider.script(FakeProvider::answer("Both done."));

    HeadlessProduct product(data());
    boot(product);
    createWorkspace(product, "Busy");
    createAgentTask(product, "Work", "Start the work.");
    const std::string id = taskId(product, "Work");
    cardAction(product, "Work", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.processes().launches.size() == 1U; }));
    // clang-format on

    cardAction(product, "Work", "ai.task.chat");
    std::string surface;
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { surface = dialog(product); return !surface.empty() && !nodes(product, "textArea", surface).empty(); }));
    // clang-format on
    emit(product, surface, "textArea", 0, "change", {{"value", "Also check the logs."}});
    emit(product, surface, "textArea", 0, "submit", json::object());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.query("SELECT id FROM ai__messages WHERE content = 'Also check the logs.'").size() == 1U; }));
    // clang-format on
    product.processes().events[0].output(process::ProcessStream::Output, "waited\n");
    product.processes().events[0].exited({0, false});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded"; }));
    // clang-format on
    const auto requests = provider.requests();
    ASSERT_EQ(requests.size(), 2U);
    EXPECT_EQ(requests[1]["messages"].back()["content"], "Also check the logs.");
    EXPECT_EQ(product.query("SELECT id FROM ai__executions WHERE task_id = ?", {id}).size(), 1U);
    product.press(ImGuiKey_Escape);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !product.product().shell().dialogs().active(); }));
    // clang-format on

    const std::string agents = "settings:ai:agents:general";
    openSettings(product, "System prompt", agents);
    emit(product, agents, "table", 0, "select", {{"id", "builder"}});
    product.frame();
    click(product, "ai.agent.remove", agents);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().shell().dialogs().active(); }));
    // clang-format on
    ASSERT_TRUE(product.answerDialog("confirm"));
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.product().preferences().document("ai")["agents"].empty(); }));
    // clang-format on
    ASSERT_TRUE(product.navigate("ai:tasks").hasValue());
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(view)).showsKey("ai.task.last-error"); }));
    // clang-format on
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// The file tools write, edit, copy, move, find and remove inside the working directory, and a path outside it is refused to the model rather than reached.
// A tool that answers nothing is told as such so the conversation goes on, and a file too large to be read whole is refused instead of edited in part.
TEST_F(AiPluginTest, RunsTheFileToolsInsideTheWorkingDirectory) {
    FakeProvider provider;
    store({{"connections", json::array({localConnection(provider)})}, {"defaultConnectionKey", "ollama/fake-large"}, {"agents", json::array({agent("ollama/fake-large", 0)})}, {"mcpServers", json::array()}, {"speechProvider", "openai"}, {"speechVoiceId", "alloy"}, {"speechApiKey", "key"}});
    // clang-format off
    const auto call = [](std::string id, std::string name, json arguments) { return json{{"id", std::move(id)}, {"name", std::move(name)}, {"arguments", std::move(arguments)}}; };
    // clang-format on
    provider.script(FakeProvider::calls(json::array({call("c1", "create_directory", {{"path", "docs"}}), call("c14", "create_directory", {{"path", "empty"}})})));
    provider.script(FakeProvider::calls(json::array({call("c2", "write_file", {{"path", "docs/plan.md"}, {"content", "alpha\nbeta\n"}})})));
    provider.script(FakeProvider::calls(json::array({call("c3", "edit_file", {{"path", "docs/plan.md"}, {"old_text", "beta"}, {"new_text", "gamma"}}), call("c4", "copy_file", {{"source", "docs/plan.md"}, {"destination", "docs/copy.md"}})})));
    provider.script(FakeProvider::calls(json::array({call("c5", "move_path", {{"source", "docs/copy.md"}, {"destination", "docs/moved.md"}})})));
    provider.script(FakeProvider::calls(json::array({call("c6", "search_files", {{"pattern", "*.md"}, {"contains", "gamma"}}), call("c7", "read_file", {{"path", "../outside.txt"}}), call("c8", "remove_path", {{"path", "notes.txt"}}), call("c9", "read_file", {{"path", 7}}), call("c10", "describe_path", {{"path", "docs"}}), call("c11", "describe_path", {{"path", "docs/plan.md"}}), call("c12", "move_path", {{"source", "docs/plan.md"}, {"destination", "docs/moved.md"}}), call("c13", "generate_speech", {{"text", "Hello"}, {"path", "sounds/{voice}.mp3"}, {"voice", "../../escaped"}}), call("c15", "list_directory", {{"path", "empty"}}), call("c16", "edit_file", {{"path", "large.txt"}, {"old_text", "beta"}, {"new_text", "gamma"}})})));
    provider.script(FakeProvider::answer("Done."));
    const std::string large = std::string(1536U * 1024U, 'a') + "beta";
    std::ofstream(root() / "large.txt", std::ios::binary) << large;

    HeadlessProduct product(data());
    boot(product);
    createWorkspace(product, "Files");
    createAgentTask(product, "Organize", "Organize the notes.");
    const std::string id = taskId(product, "Organize");
    cardAction(product, "Organize", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded"; }));
    // clang-format on

    // clang-format off
    const auto read = [&](std::string_view relative) { std::ifstream file(root() / relative, std::ios::binary); return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()); };
    // clang-format on
    EXPECT_EQ(read("docs/plan.md"), "alpha\ngamma\n");
    EXPECT_EQ(read("docs/moved.md"), "alpha\ngamma\n");
    EXPECT_FALSE(std::filesystem::exists(root() / "docs/copy.md"));
    EXPECT_FALSE(std::filesystem::exists(root() / "notes.txt"));

    const auto requests = provider.requests();
    ASSERT_EQ(requests.size(), 6U);
    const json& messages = requests[5]["messages"];
    // clang-format off
    const auto answer = [&](std::string_view callId) { for (const auto& message : messages) { if (message.value("tool_call_id", "") == callId) { return message["content"].get<std::string>(); } } return std::string(); };
    // clang-format on
    EXPECT_NE(answer("c6").find("docs/plan.md"), std::string::npos);
    EXPECT_NE(answer("c6").find("docs/moved.md"), std::string::npos);
    EXPECT_NE(answer("c7").find("is outside the working directory"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(root().parent_path() / "outside.txt"));
    EXPECT_FALSE(answer("c9").empty());
    EXPECT_EQ(json::parse(answer("c10")).value("type", ""), "directory");
    EXPECT_TRUE(json::parse(answer("c10")).value("writable", false));
    EXPECT_TRUE(json::parse(answer("c11")).value("readable", false));
    EXPECT_TRUE(json::parse(answer("c11")).value("writable", false));
    EXPECT_NE(answer("c12").find("The destination \"docs/moved.md\" already exists"), std::string::npos);
    EXPECT_NE(answer("c13").find("is outside the working directory"), std::string::npos) << answer("c13");
    EXPECT_EQ(answer("c15"), "The tool returned nothing");
    EXPECT_NE(answer("c16").find("is larger than one mebibyte"), std::string::npos) << answer("c16");
    EXPECT_EQ(read("large.txt"), large);
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

#if !defined(_WIN32)
// A path through a link to nothing is refused, since a write through it would create its target wherever it points, and removing a link removes the link and leaves what it points at.
TEST_F(AiPluginTest, KeepsTheFileToolsOffLinksToNothingAndRemovesLinksThemselves) {
    FakeProvider provider;
    store({{"connections", json::array({localConnection(provider)})}, {"defaultConnectionKey", "ollama/fake-large"}, {"agents", json::array({agent("ollama/fake-large", 0)})}, {"mcpServers", json::array()}});
    std::filesystem::create_directories(root() / "releases" / "v2");
    std::ofstream(root() / "releases" / "v2" / "app.txt") << "app";
    std::filesystem::create_directory_symlink(root() / "releases" / "v2", root() / "current");
    TemporaryDirectory outside;
    const std::filesystem::path escaped = outside.path() / "escaped.txt";
    std::filesystem::create_symlink(escaped, root() / "dangling");
    // clang-format off
    const auto call = [](std::string id, std::string name, json arguments) { return json{{"id", std::move(id)}, {"name", std::move(name)}, {"arguments", std::move(arguments)}}; };
    // clang-format on
    provider.script(FakeProvider::calls(json::array({call("c1", "write_file", {{"path", "dangling"}, {"content", "outside"}}), call("c2", "remove_path", {{"path", "current"}, {"recursive", true}})})));
    provider.script(FakeProvider::answer("Done."));

    HeadlessProduct product(data());
    boot(product);
    createWorkspace(product, "Links");
    createAgentTask(product, "Tidy", "Tidy the releases.");
    const std::string id = taskId(product, "Tidy");
    cardAction(product, "Tidy", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded"; }));
    // clang-format on

    const auto requests = provider.requests();
    ASSERT_EQ(requests.size(), 2U);
    const json& messages = requests[1]["messages"];
    // clang-format off
    const auto answer = [&](std::string_view callId) { for (const auto& message : messages) { if (message.value("tool_call_id", "") == callId) { return message["content"].get<std::string>(); } } return std::string(); };
    // clang-format on
    EXPECT_NE(answer("c1").find("goes through a link that points to nothing"), std::string::npos) << answer("c1");
    EXPECT_FALSE(std::filesystem::exists(escaped));
    EXPECT_FALSE(std::filesystem::is_symlink(std::filesystem::symlink_status(root() / "current")));
    EXPECT_TRUE(std::filesystem::exists(root() / "releases" / "v2" / "app.txt"));
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}
#endif

// The skills and the instructions a project carries join the prompt of the agent, and the skill tools read a skill and the files beside it.
TEST_F(AiPluginTest, OffersTheSkillsAndInstructionsOfTheProject) {
    // clang-format off
    const auto write = [&](std::string_view relative, std::string_view content) { std::filesystem::create_directories((root() / relative).parent_path()); std::ofstream(root() / relative, std::ios::binary) << content; };
    // clang-format on
    write(".claude/skills/deploy/SKILL.md", "---\nname: deploy\ndescription: Ships the site\n---\nRun the deploy script.\n");
    write(".claude/skills/deploy/reference.md", "Deploy with care.\n");
    write("skills/lint.md", "---\ndescription: Checks the style\n---\nRun the linter.\n");
    write("skills/undocumented.md", "---\nname: undocumented\n---\nNothing.\n");
    write(".claude/plugins/kit/plugin.json", "{}");
    write(".claude/plugins/kit/skills/format/SKILL.md", "---\ndescription: Formats the code\n---\nRun the formatter.\n");
    write("AGENTS.md", "Always write tests.\n");

    FakeProvider provider;
    json configured = agent("ollama/fake-large", 8);
    configured["systemPrompt"] = "Skills:\n{{SKILLS}}\nContext:\n{{CONTEXT_FILES}}";
    store({{"connections", json::array({localConnection(provider)})}, {"agents", json::array({configured})}});
    // clang-format off
    const auto call = [](std::string id, std::string name, json arguments) { return json{{"id", std::move(id)}, {"name", std::move(name)}, {"arguments", std::move(arguments)}}; };
    // clang-format on
    provider.script(FakeProvider::calls(json::array({call("s1", "read_skill", {{"name", "deploy"}}), call("s2", "read_skill_file", {{"name", "deploy"}, {"path", "reference.md"}}), call("s3", "read_skill_file", {{"name", "deploy"}, {"path", "../../../AGENTS.md"}}), call("s4", "search_skills", {{"query", "style"}})})));
    provider.script(FakeProvider::answer("Ready."));

    HeadlessProduct product(data());
    boot(product);
    createWorkspace(product, "Skills");
    createAgentTask(product, "Ship", "Ship it.");
    const std::string id = taskId(product, "Ship");
    cardAction(product, "Ship", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded"; }));
    // clang-format on

    const auto requests = provider.requests();
    ASSERT_EQ(requests.size(), 2U);
    const std::string instructions = requests[0]["messages"][0]["content"];
    EXPECT_NE(instructions.find("- deploy: Ships the site"), std::string::npos);
    EXPECT_NE(instructions.find("- lint: Checks the style"), std::string::npos);
    EXPECT_NE(instructions.find("- format: Formats the code"), std::string::npos);
    EXPECT_EQ(instructions.find("undocumented"), std::string::npos);
    EXPECT_NE(instructions.find("Instructions from \"AGENTS.md\":\n\nAlways write tests."), std::string::npos);

    const json& messages = requests[1]["messages"];
    // clang-format off
    const auto answer = [&](std::string_view callId) { for (const auto& message : messages) { if (message.value("tool_call_id", "") == callId) { return message["content"].get<std::string>(); } } return std::string(); };
    // clang-format on
    EXPECT_NE(answer("s1").find("Run the deploy script."), std::string::npos);
    EXPECT_EQ(answer("s2"), "Deploy with care.\n");
    EXPECT_NE(answer("s3").find("is outside the working directory"), std::string::npos);
    EXPECT_NE(answer("s4").find("lint: Checks the style"), std::string::npos);
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A page is fetched as its readable words, a search is asked of the configured service, and an address the tool may not reach is refused to the model.
TEST_F(AiPluginTest, SearchesAndReadsTheWeb) {
    httplib::Server web;
    // clang-format off
    web.Get("/page", [](const httplib::Request&, httplib::Response& response) { response.set_content("<html><head><style>p{}</style><script>alert(1)</script></head><body><h1>Title</h1><p>Hello   world</p></body></html>", "text/html"); });
    web.Get("/search", [](const httplib::Request& request, httplib::Response& response) { response.set_content(json{{"results", json::array({{{"title", "Result for " + request.get_param_value("q")}, {"url", "https://example.com"}, {"content", "<b>Fast</b> desktop"}}})}}.dump(), "application/json"); });
    // clang-format on
    const int port = LocalPort::bind(web);
    // clang-format off
    std::thread thread([&web]() { std::ignore = web.listen_after_bind(); });
    // clang-format on
    web.wait_until_ready();
    const std::string address = "http://127.0.0.1:" + std::to_string(port);

    FakeProvider provider;
    store({{"connections", json::array({localConnection(provider)})}, {"agents", json::array({agent("ollama/fake-large", 8)})}, {"searchProvider", "searxng"}, {"searchInstanceUrl", address}});
    // clang-format off
    const auto call = [](std::string id, std::string name, json arguments) { return json{{"id", std::move(id)}, {"name", std::move(name)}, {"arguments", std::move(arguments)}}; };
    // clang-format on
    provider.script(FakeProvider::calls(json::array({call("w1", "fetch_url", {{"url", address + "/page"}}), call("w2", "web_search", {{"query", "workpane"}, {"count", 1}}), call("w3", "fetch_url", {{"url", "ftp://files.example.com"}})})));
    provider.script(FakeProvider::answer("Read."));

    HeadlessProduct product(data());
    boot(product);
    createWorkspace(product, "Web");
    createAgentTask(product, "Browse", "Look it up.");
    const std::string id = taskId(product, "Browse");
    cardAction(product, "Browse", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded"; }));
    // clang-format on
    web.stop();
    thread.join();

    const auto requests = provider.requests();
    ASSERT_EQ(requests.size(), 2U);
    // clang-format off
    EXPECT_TRUE(std::ranges::any_of(requests[0]["tools"], [](const json& tool) { return tool["function"]["name"] == "web_search"; }));
    // clang-format on
    const json& messages = requests[1]["messages"];
    // clang-format off
    const auto answer = [&](std::string_view callId) { for (const auto& message : messages) { if (message.value("tool_call_id", "") == callId) { return message["content"].get<std::string>(); } } return std::string(); };
    // clang-format on
    EXPECT_EQ(answer("w1"), "Title Hello world");
    EXPECT_EQ(answer("w2"), "Result for workpane\nhttps://example.com\nFast desktop");
    EXPECT_FALSE(answer("w3").empty());
    EXPECT_EQ(answer("w3").find("Title"), std::string::npos);
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A server reached over streamable HTTP keeps its session, answers in JSON or in an event stream, and its resources, prompts and tools reach the agent.
TEST_F(AiPluginTest, ReachesAnMcpServerOverHttp) {
    httplib::Server remote;
    std::mutex mutex;
    std::vector<std::string> sessions;
    std::vector<std::string> authorizations;
    // clang-format off
    remote.Post("/mcp", [&](const httplib::Request& request, httplib::Response& response) {
        const json message = json::parse(request.body);
        const std::string method = message.value("method", "");

        {
            const std::lock_guard lock(mutex);
            sessions.push_back(request.get_header_value("mcp-session-id"));
            authorizations.push_back(request.get_header_value("Authorization"));
        }

        if (!message.contains("id")) {
            response.status = 202;
            return;
        }

        json result = json::object();

        if (method == "initialize") {
            response.set_header("Mcp-Session-Id", "session-1");
            result = {{"protocolVersion", "2025-06-18"}, {"capabilities", {{"tools", json::object()}, {"resources", json::object()}, {"prompts", json::object()}}}, {"serverInfo", {{"name", "remote"}, {"version", "1"}}}};
        } else if (method == "tools/list") {
            result = {{"tools", json::array({{{"name", "echo"}, {"description", "Echo"}, {"inputSchema", {{"type", "object"}, {"properties", json::object()}}}}})}};
        } else if (method == "resources/list") {
            result = {{"resources", json::array({{{"uri", "notes://today"}, {"name", "today"}, {"description", "The notes of today"}}})}};
        } else if (method == "resources/read") {
            result = {{"contents", json::array({{{"uri", message["params"]["uri"]}, {"text", "Buy milk"}}})}};
        } else if (method == "prompts/list") {
            result = {{"prompts", json::array({{{"name", "summary"}, {"description", "Summarize a topic"}}})}};
        } else if (method == "prompts/get") {
            result = {{"messages", json::array({{{"role", "user"}, {"content", {{"type", "text"}, {"text", "Summarize " + message["params"]["arguments"]["topic"].get<std::string>()}}}}})}};
        } else if (method == "tools/call") {
            result = {{"content", json::array({{{"type", "text"}, {"text", "echoed"}}})}};
        }

        const std::string answer = json{{"jsonrpc", "2.0"}, {"id", message["id"]}, {"result", result}}.dump();

        if (method == "tools/list") {
            response.set_content("event: message\ndata: " + answer + "\n\n", "text/event-stream");
            return;
        }

        response.set_content(answer, "application/json");
    });
    // clang-format on

    const int port = LocalPort::bind(remote);
    // clang-format off
    std::thread thread([&remote]() { std::ignore = remote.listen_after_bind(); });
    // clang-format on
    remote.wait_until_ready();

    FakeProvider provider;
    const json server = {{"id", "remote"}, {"transport", "http"}, {"command", ""}, {"arguments", json::array()}, {"workdir", ""}, {"url", "http://127.0.0.1:" + std::to_string(port) + "/mcp"}, {"apiKey", "token-1"}, {"roots", json::array()}, {"samplingEnabled", false}, {"samplingMaximumTokens", 0}};
    configure(provider, 8, json::array({server}));
    // clang-format off
    const auto call = [](std::string id, std::string name, json arguments) { return json{{"id", std::move(id)}, {"name", std::move(name)}, {"arguments", std::move(arguments)}}; };
    // clang-format on
    provider.script(FakeProvider::calls(json::array({call("m1", "list_mcp_resources", json::object()), call("m2", "read_mcp_resource", {{"server", "remote"}, {"uri", "notes://today"}}), call("m3", "list_mcp_prompts", json::object()), call("m4", "read_mcp_prompt", {{"server", "remote"}, {"name", "summary"}, {"arguments", {{"topic", "the week"}}}}), call("m5", "mcp_remote_echo", json::object())})));
    provider.script(FakeProvider::answer("Collected."));

    HeadlessProduct product(data());
    boot(product);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { const std::lock_guard lock(mutex); return sessions.size() >= 3U; }));
    // clang-format on
    product.settle(std::chrono::milliseconds(100));
    createWorkspace(product, "Remote");
    createAgentTask(product, "Gather", "Gather what the server knows.");
    const std::string id = taskId(product, "Gather");
    cardAction(product, "Gather", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded"; }));
    // clang-format on
    product.stop();
    remote.stop();
    thread.join();

    const auto requests = provider.requests();
    const json& messages = requests[1]["messages"];
    // clang-format off
    const auto answer = [&](std::string_view callId) { for (const auto& message : messages) { if (message.value("tool_call_id", "") == callId) { return message["content"].get<std::string>(); } } return std::string(); };
    // clang-format on
    EXPECT_EQ(answer("m1"), "remote | notes://today | The notes of today");
    EXPECT_EQ(answer("m2"), "Buy milk");
    EXPECT_EQ(answer("m3"), "remote | summary | Summarize a topic");
    EXPECT_EQ(answer("m4"), "Summarize the week");
    EXPECT_EQ(answer("m5"), "echoed");
    EXPECT_EQ(sessions.front(), "");
    // clang-format off
    EXPECT_TRUE(std::all_of(sessions.begin() + 1, sessions.end(), [](const std::string& session) { return session == "session-1"; }));
    EXPECT_TRUE(std::ranges::all_of(authorizations, [](const std::string& authorization) { return authorization == "Bearer token-1"; }));
    // clang-format on
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A server over HTTP reads its key from the environment, a request that fails answers only itself while another one still waits for its answer, and an expired session is started again.
TEST_F(AiPluginTest, KeepsAnMcpSessionOverHttpThroughFailures) {
    httplib::Server remote;
    std::mutex mutex;
    int initialized = 0;
    std::vector<std::string> authorizations;
    // clang-format off
    remote.Post("/mcp", [&](const httplib::Request& request, httplib::Response& response) {
        const json message = json::parse(request.body);
        const std::string method = message.value("method", "");
        const std::string session = request.get_header_value("mcp-session-id");

        {
            const std::lock_guard lock(mutex);
            authorizations.push_back(request.get_header_value("Authorization"));
        }

        if (!message.contains("id")) {
            response.status = 202;
            return;
        }

        if (method == "resources/list") {
            response.status = 500;
            return;
        }

        if (method == "tools/call" && session == "session-1") {
            response.status = 404;
            return;
        }

        json result = json::object();

        if (method == "initialize") {
            const std::lock_guard lock(mutex);
            response.set_header("Mcp-Session-Id", "session-" + std::to_string(++initialized));
            result = {{"protocolVersion", "2025-06-18"}, {"capabilities", {{"tools", json::object()}, {"resources", json::object()}}}, {"serverInfo", {{"name", "remote"}, {"version", "1"}}}};
        } else if (method == "tools/list") {
            result = {{"tools", json::array({{{"name", "echo"}, {"description", "Echo"}, {"inputSchema", {{"type", "object"}, {"properties", json::object()}}}}})}};
        } else if (method == "resources/read") {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            result = {{"contents", json::array({{{"uri", message["params"]["uri"]}, {"text", "Buy milk"}}})}};
        } else if (method == "tools/call") {
            result = {{"content", json::array({{{"type", "text"}, {"text", "echoed by " + session}}})}};
        }

        response.set_content(json{{"jsonrpc", "2.0"}, {"id", message["id"]}, {"result", result}}.dump(), "application/json");
    });
    // clang-format on

    const int port = LocalPort::bind(remote);
    // clang-format off
    std::thread thread([&remote]() { std::ignore = remote.listen_after_bind(); });
    // clang-format on
    remote.wait_until_ready();
#if defined(_WIN32)
    ASSERT_EQ(_putenv_s("WORKPANE_MCP_TEST_TOKEN", "token-env"), 0);
#else
    ASSERT_EQ(::setenv("WORKPANE_MCP_TEST_TOKEN", "token-env", 1), 0);
#endif

    FakeProvider provider;
    const json server = {{"id", "remote"}, {"transport", "http"}, {"command", ""}, {"arguments", json::array()}, {"workdir", ""}, {"url", "http://127.0.0.1:" + std::to_string(port) + "/mcp"}, {"apiKey", "{env.WORKPANE_MCP_TEST_TOKEN}"}, {"roots", json::array()}, {"samplingEnabled", false}, {"samplingMaximumTokens", 0}};
    configure(provider, 8, json::array({server}));
    // clang-format off
    const auto call = [](std::string id, std::string name, json arguments) { return json{{"id", std::move(id)}, {"name", std::move(name)}, {"arguments", std::move(arguments)}}; };
    // clang-format on
    provider.script(FakeProvider::calls(json::array({call("r1", "list_mcp_resources", json::object()), call("r2", "read_mcp_resource", {{"server", "remote"}, {"uri", "notes://today"}})})));
    provider.script(FakeProvider::calls(json::array({call("t1", "mcp_remote_echo", json::object())})));
    provider.script(FakeProvider::answer("Tried."));

    HeadlessProduct product(data());
    boot(product);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { const std::lock_guard lock(mutex); return initialized == 1 && authorizations.size() >= 3U; }));
    // clang-format on
    product.settle(std::chrono::milliseconds(100));
    createWorkspace(product, "Remote");
    createAgentTask(product, "Gather", "Gather what the server knows.");
    const std::string id = taskId(product, "Gather");
    cardAction(product, "Gather", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded"; }));
    ASSERT_TRUE(product.frameUntil([&]() { const std::lock_guard lock(mutex); return initialized == 2; }));
    // clang-format on
    product.settle(std::chrono::milliseconds(300));

    provider.script(FakeProvider::calls(json::array({call("t2", "mcp_remote_echo", json::object())})));
    provider.script(FakeProvider::answer("Echoed."));
    cardAction(product, "Gather", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return provider.requests().size() == 5U && status(product, id) == "succeeded"; }));
    // clang-format on
    product.stop();
    remote.stop();
    thread.join();

    const auto requests = provider.requests();
    // clang-format off
    const auto answer = [&](std::size_t turn, std::string_view callId) { for (const auto& message : requests[turn]["messages"]) { if (message.value("tool_call_id", "") == callId) { return message["content"].get<std::string>(); } } return std::string(); };
    // clang-format on
    EXPECT_EQ(answer(1, "r2"), "Buy milk");
    EXPECT_NE(answer(1, "r1"), "");
    EXPECT_NE(answer(2, "t1").find("no longer valid"), std::string::npos) << answer(2, "t1");
    EXPECT_EQ(answer(4, "t2"), "echoed by session-2");
    // clang-format off
    EXPECT_TRUE(std::ranges::all_of(authorizations, [](const std::string& authorization) { return authorization == "Bearer token-env"; }));
    // clang-format on
}

// A task keeps the hundred runs its view lists, so a new run removes the oldest one together with its log.
TEST_F(AiPluginTest, KeepsTheRunsTheViewOfATaskLists) {
    std::string id;

    {
        HeadlessProduct product(data());
        boot(product);
        createWorkspace(product, "Runs");
        createCommandTask(product, "Echo", "echo hi");
        id = taskId(product, "Echo");
        product.stop();
    }

    {
        auto database = openDatabase();

        // clang-format off
        const auto padded = [](int value) { return (value < 10 ? std::string("0") : std::string()) + std::to_string(value); };
        // clang-format on

        for (int run = 0; run < 100; ++run) {
            const std::string started = "2020-01-01T00:" + padded(run / 60) + ":" + padded(run % 60) + ".000Z";
            const std::vector<json> bindings = {"old-" + std::to_string(run), id, "succeeded", started, started, 0, 0, "0", "", "", "answered", "", ""};
            ASSERT_TRUE(database.run("INSERT INTO ai__executions(id, task_id, status, started_at_utc, finished_at_utc, input_tokens, output_tokens, finish_reason, error_message, content, stop_reason, provider_id, model_id) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", bindings).hasValue());
        }

        ASSERT_TRUE(database.run("INSERT INTO ai__logs(id, execution_id, sequence, timestamp_utc, level, kind, detail) VALUES('l0', 'old-0', 1, '2020-01-01T00:00:00.000Z', 'info', 'started', 'echo hi')").hasValue());
    }

    HeadlessProduct product(data());
    boot(product);
    cardAction(product, "Echo", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return product.processes().launches.size() == 1U; }));
    // clang-format on
    product.processes().events[0].exited({0, false});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded"; }));
    // clang-format on

    EXPECT_EQ(product.query("SELECT id FROM ai__executions WHERE task_id = ?", {id}).size(), 100U);
    EXPECT_TRUE(product.query("SELECT id FROM ai__executions WHERE id = 'old-0'").empty());
    EXPECT_FALSE(product.query("SELECT id FROM ai__executions WHERE id = 'old-1'").empty());
    EXPECT_TRUE(product.query("SELECT id FROM ai__logs WHERE execution_id = 'old-0'").empty());
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A run the last session left marked running reads as cancelled on its card, as the store closes it, instead of a run nobody is running.
TEST_F(AiPluginTest, ShowsARunTheLastSessionLeftRunningAsCancelled) {
    std::string id;

    {
        HeadlessProduct product(data());
        boot(product);
        createWorkspace(product, "Interrupted");
        createCommandTask(product, "Build", "make all");
        id = taskId(product, "Build");
        product.stop();
    }

    {
        auto database = openDatabase();
        const std::vector<json> bindings = {"x1", id, "running", "2026-01-01T00:00:00.000Z", "", 0, 0, "", "", "", "answered", "", ""};
        ASSERT_TRUE(database.run("INSERT INTO ai__executions(id, task_id, status, started_at_utc, finished_at_utc, input_tokens, output_tokens, finish_reason, error_message, content, stop_reason, provider_id, model_id) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", bindings).hasValue());
    }

    HeadlessProduct product(data());
    boot(product);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return SurfaceReader(product.declared(view)).showsText("Build"); }));
    // clang-format on
    product.settle(std::chrono::milliseconds(200));

    EXPECT_TRUE(SurfaceReader(product.declared(view)).showsKey("ai.badge.cancelled"));
    EXPECT_FALSE(SurfaceReader(product.declared(view)).showsKey("ai.badge.running"));
    EXPECT_EQ(product.query("SELECT status FROM ai__executions WHERE id = 'x1'")[0]["status"], "cancelled");
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A run the last session left queued starts once the servers have come up, so it offers the model the tools of every server.
TEST_F(AiPluginTest, StartsTheRunsLeftQueuedWithTheToolsOfTheServers) {
    FakeProvider provider;
    const json server = {{"id", "helper"}, {"transport", "stdio"}, {"command", "helper-server"}, {"arguments", json::array()}, {"workdir", ""}, {"url", ""}, {"apiKey", ""}, {"roots", json::array()}, {"samplingEnabled", false}, {"samplingMaximumTokens", 0}};
    configure(provider, 8, json::array({server}));
    std::string id;

    {
        HeadlessProduct product(data());
        product.processes().executables["helper-server"] = RootedPath::of("opt/tools/helper-server");
        serveMcp(product);
        boot(product);
        createWorkspace(product, "Launch");
        createAgentTask(product, "Resume", "Resume the work.");
        id = taskId(product, "Resume");
        product.stop();
    }

    {
        auto database = openDatabase();
        ASSERT_TRUE(database.run("UPDATE ai__tasks SET column_name = 'doing'").hasValue());
        const std::vector<json> bindings = {"r1", id, 1, "user", "Resume the work.", "[]", "", 0, "[]", "2026-01-01T00:00:00.000Z"};
        ASSERT_TRUE(database.run("INSERT INTO ai__messages(id, task_id, sequence, role, content, tool_calls, tool_call_id, summarized_until, parts_json, created_at_utc) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", bindings).hasValue());
    }

    provider.script(FakeProvider::answer("Resumed."));
    HeadlessProduct product(data());
    product.processes().executables["helper-server"] = RootedPath::of("opt/tools/helper-server");
    serveMcp(product);
    boot(product);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded"; }));
    // clang-format on

    const auto requests = provider.requests();
    ASSERT_EQ(requests.size(), 1U);
    // clang-format off
    EXPECT_TRUE(std::ranges::any_of(requests[0]["tools"], [](const json& tool) { return tool["function"]["name"] == "mcp_helper_lookup"; }));
    // clang-format on
    product.stop();
}

// A server asks the client for its roots and for a sampled answer, which the default connection gives, and a request still pending when it stops is cancelled at the server.
TEST_F(AiPluginTest, AnswersWhatAnMcpServerAsks) {
    FakeProvider provider;
    const json server = {{"id", "helper"}, {"transport", "stdio"}, {"command", "helper-server"}, {"arguments", json::array()}, {"workdir", ""}, {"url", ""}, {"apiKey", ""}, {"roots", json::array({root().generic_string()})}, {"samplingEnabled", true}, {"samplingMaximumTokens", 256}};
    configure(provider, 8, json::array({server}));
    provider.script(FakeProvider::answer("Hi from the model."));

    HeadlessProduct product(data());
    product.processes().executables["helper-server"] = RootedPath::of("opt/tools/helper-server");
    serveMcp(product);
    boot(product);
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return received("tools/list").size() == 1U; }));
    // clang-format on
    // clang-format off
    const auto ask = [&](const json& message) { product.processes().events[0].output(process::ProcessStream::Output, message.dump() + "\n"); };
    // clang-format on
    ask({{"jsonrpc", "2.0"}, {"id", "r1"}, {"method", "roots/list"}});
    ask({{"jsonrpc", "2.0"}, {"id", "s1"}, {"method", "sampling/createMessage"}, {"params", {{"messages", json::array({{{"role", "user"}, {"content", {{"type", "text"}, {"text", "Say hi"}}}}})}, {"maxTokens", 50}}}});
    ask({{"jsonrpc", "2.0"}, {"id", "u1"}, {"method", "elicitation/create"}});
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return !reply("r1").is_null() && !reply("s1").is_null() && !reply("u1").is_null(); }));
    // clang-format on
    EXPECT_EQ(reply("r1")["result"]["roots"][0]["uri"], FileAddress::of(root()));
    EXPECT_EQ(reply("s1")["result"]["content"]["text"], "Hi from the model.");
    EXPECT_EQ(reply("u1")["error"]["code"], -32601);
    EXPECT_EQ(provider.requests()[0]["max_tokens"], 256);
    EXPECT_EQ(provider.requests()[0]["messages"][0]["content"], "Say hi");

    provider.script(FakeProvider::calls(json::array({{{"id", "h1"}, {"name", "mcp_helper_hang"}, {"arguments", json::object()}}})));
    createWorkspace(product, "Helper");
    createAgentTask(product, "Wait", "Wait for the server.");
    cardAction(product, "Wait", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return received("tools/call").size() == 1U; }));
    // clang-format on
    product.stop();
    const auto cancelled = received("notifications/cancelled");
    ASSERT_EQ(cancelled.size(), 1U);
    EXPECT_EQ(cancelled[0]["params"]["requestId"], received("tools/call")[0]["id"]);
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A run reads the conversation from storage since its newest summary, so a conversation longer than the page the chat loads reaches the model whole.
TEST_F(AiPluginTest, RunsOnTheWholeConversationRatherThanTheLoadedPage) {
    FakeProvider provider;
    configure(provider);
    std::string id;

    {
        HeadlessProduct product(data());
        boot(product);
        createWorkspace(product, "History");
        createAgentTask(product, "Long", "Continue.");
        id = taskId(product, "Long");
        product.stop();
    }

    {
        auto database = openDatabase();

        for (int sequence = 1; sequence <= 150; ++sequence) {
            const std::vector<json> bindings = {"m" + std::to_string(sequence), id, sequence, sequence % 2 == 1 ? "user" : "assistant", "m" + std::to_string(sequence), "[]", "", 0, "[]", "2026-01-01T00:00:00.000Z"};
            ASSERT_TRUE(database.run("INSERT INTO ai__messages(id, task_id, sequence, role, content, tool_calls, tool_call_id, summarized_until, parts_json, created_at_utc) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", bindings).hasValue());
        }
    }

    provider.script(FakeProvider::answer("Continued."));
    HeadlessProduct product(data());
    boot(product);
    cardAction(product, "Long", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded"; }));
    // clang-format on

    const auto requests = provider.requests();
    ASSERT_EQ(requests.size(), 1U);
    const json& messages = requests[0]["messages"];
    // clang-format off
    const auto carries = [&messages](std::string_view text) { return std::ranges::any_of(messages, [&text](const json& message) { return message["content"].is_string() && message["content"].get<std::string>() == text; }); };
    // clang-format on
    EXPECT_TRUE(carries("m1"));
    EXPECT_TRUE(carries("m50"));
    EXPECT_TRUE(carries("m150"));
    EXPECT_EQ(messages.back()["content"], "Continue.");
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

// A conversation longer than the window of the model is summarized: it keeps its opening, the oldest turns after it are sent alone to be summed up, and the run goes on with the summary in their place.
// A later run still opens with the first message and the summary right after it, followed by every turn the summary does not cover.
TEST_F(AiPluginTest, SummarizesAConversationThatOutgrowsTheWindow) {
    FakeProvider provider;
    json connection = localConnection(provider);
    connection["modelId"] = "llama3.1";
    store({{"connections", json::array({connection})}, {"agents", json::array({agent("ollama/llama3.1", 8)})}});
    std::string id;

    {
        HeadlessProduct product(data());
        boot(product);
        createWorkspace(product, "Memory");
        createAgentTask(product, "Recall", "Recall everything.");
        id = taskId(product, "Recall");
        product.stop();
    }

    {
        auto database = openDatabase();
        std::string paragraph;

        for (int word = 0; word < 400; ++word) {
            paragraph += "word ";
        }

        for (int sequence = 1; sequence <= 20; ++sequence) {
            const std::vector<json> bindings = {"h" + std::to_string(sequence), id, sequence, sequence % 2 == 1 ? "user" : "assistant", std::to_string(sequence) + " " + paragraph, "[]", "", 0, "[]", "2026-01-01T00:00:00.000Z"};
            ASSERT_TRUE(database.run("INSERT INTO ai__messages(id, task_id, sequence, role, content, tool_calls, tool_call_id, summarized_until, parts_json, created_at_utc) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", bindings).hasValue());
        }
    }

    provider.script(FakeProvider::answer("Earlier they discussed plans."));
    provider.script(FakeProvider::answer("Recalled."));
    HeadlessProduct product(data());
    boot(product);
    cardAction(product, "Recall", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return status(product, id) == "succeeded"; }));
    // clang-format on

    const auto requests = provider.requests();
    ASSERT_EQ(requests.size(), 2U);
    EXPECT_FALSE(requests[0].contains("tools"));
    EXPECT_EQ(requests[0]["messages"].size(), 1U);
    EXPECT_NE(requests[0]["messages"][0]["content"].get<std::string>().find("1 word word"), std::string::npos);
    const json& messages = requests[1]["messages"];
    // clang-format off
    EXPECT_TRUE(std::ranges::any_of(messages, [](const json& message) { return message["content"].is_string() && message["content"].get<std::string>().find("Earlier they discussed plans.") != std::string::npos; }));
    EXPECT_TRUE(std::ranges::none_of(messages, [](const json& message) { return message["content"].is_string() && message["content"].get<std::string>().starts_with("2 word"); }));
    // clang-format on
    EXPECT_EQ(messages.back()["content"], "Recall everything.");
    const auto summaries = product.query("SELECT summarized_until FROM ai__messages WHERE task_id = ? AND summarized_until > 0", {id});
    ASSERT_EQ(summaries.size(), 1U);
    const int covered = summaries[0]["summarized_until"].get<int>();
    EXPECT_GT(covered, 1);
    EXPECT_EQ(product.query("SELECT stop_reason FROM ai__executions WHERE task_id = ?", {id})[0]["stop_reason"], "answered");

    provider.script(FakeProvider::answer("Recalled again."));
    cardAction(product, "Recall", "ai.task.start");
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { return provider.requests().size() == 3U && status(product, id) == "succeeded"; }));
    // clang-format on
    const json later = provider.requests()[2]["messages"];
    ASSERT_GE(later.size(), 4U);
    EXPECT_EQ(later[0]["role"], "system");
    EXPECT_TRUE(later[1]["content"].get<std::string>().starts_with("1 word"));
    EXPECT_NE(later[2]["content"].get<std::string>().find("Earlier they discussed plans."), std::string::npos);

    for (int sequence = covered + 1; sequence <= 20; ++sequence) {
        const std::string opening = std::to_string(sequence) + " word";
        // clang-format off
        EXPECT_TRUE(std::ranges::any_of(later, [&opening](const json& message) { return message["content"].is_string() && message["content"].get<std::string>().starts_with(opening); })) << opening;
        // clang-format on
    }

    EXPECT_EQ(later.back()["content"], "Recall everything.");

    // The summary reads as a note of the conversation, never as a message of the reader.
    cardAction(product, "Recall", "ai.task.chat");
    std::string surface;
    // clang-format off
    ASSERT_TRUE(product.frameUntil([&]() { surface = dialog(product); return !surface.empty() && SurfaceReader(product.declared(surface)).showsKey("ai.conversation.summary"); }));
    // clang-format on
    std::vector<json> summarized;

    for (const auto node : nodes(product, "markdown", surface)) {
        const json props = properties(product, node, surface);

        if (props.contains("text") && props["text"].is_string() && props["text"].get<std::string>().find("Earlier they discussed plans.") != std::string::npos) {
            summarized.push_back(props);
        }
    }

    ASSERT_EQ(summarized.size(), 1U);
    EXPECT_EQ(summarized[0]["color"], "text-muted");
    product.stop();
    EXPECT_TRUE(errors(product).empty()) << json(errors(product)).dump();
}

} // namespace workpane::tests
