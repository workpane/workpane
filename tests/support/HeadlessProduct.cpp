#include "support/HeadlessProduct.h"

#include "app/ApplicationPaths.h"
#include "app/Product.h"
#include "persistence/Database.h"
#include "persistence/DatabaseBootstrap.h"
#include "scripting/ScriptRuntime.h"
#include "support/FakeAudioOutput.h"
#include "support/FakeDialogService.h"
#include "support/FakeNativeViewHost.h"
#include "support/FakeProcessLauncher.h"
#include "support/FakePseudoTerminalHost.h"
#include "support/FakeSystemInspector.h"
#include "support/FakeSystemServices.h"
#include "support/Resources.h"
#include "support/RootedPath.h"
#include "ui/NativeViewHost.h"
#include "ui/model/Surface.h"
#include "ui/model/SurfaceStore.h"
#include "ui/shell/Shell.h"
#include "ui/theme/Theme.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_impl_null.h>
#include <imgui_internal.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>

namespace workpane::tests {

HeadlessProduct::HeadlessProduct(std::filesystem::path data, std::string locale, std::filesystem::path resources) : m_data(std::move(data)), m_resources(std::move(resources)) {
    m_system->locale = std::move(locale);

#if defined(_WIN32)
    // Every Windows finds its command prompt, which runs the commands a plugin writes as one line.
    m_processes->executables["cmd"] = RootedPath::of("Windows/System32/cmd.exe");
#endif
}

HeadlessProduct::~HeadlessProduct() {
    stop();
    reportErrors();

    if (m_imguiReady) {
        ImGui_ImplNullRender_Shutdown();
        m_product->releaseTextures();
        ImGui_ImplNullPlatform_Shutdown();
        ImGui::DestroyContext();
    }
}

// Boots exactly as the application does, then draws until every plugin has started and the shell leaves its loading state.
Result<void> HeadlessProduct::boot() {
    auto opened = app::Product::open(app::ApplicationPaths{std::filesystem::path(), m_resources, m_data}, std::make_unique<FakeSystemServices>(m_system), std::make_unique<FakeSystemInspector>(m_system), std::make_unique<FakePseudoTerminalHost>(m_terminals), std::make_unique<FakeProcessLauncher>(m_processes), std::make_unique<FakeAudioOutput>(m_audio));

    if (!opened.hasValue()) {
        return Result<void>::failure(opened.error());
    }

    m_product = std::move(opened.value());
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplNullPlatform_Init();
    ImGui_ImplNullRender_Init();
    m_imguiReady = true;

    // clang-format off
    if (const auto attached = m_product->attach({*io.Fonts, std::make_unique<FakeDialogService>(m_dialogs), std::make_unique<FakeNativeViewHost>(m_webViews), 1.0F, []() {}, [](const ui::Theme&) {}, [system = m_system]() { return system->displays; }}); !attached.hasValue()) {
        return attached;
    }
    // clang-format on

    if (const auto started = m_product->start(); !started.hasValue()) {
        return started;
    }

    // Plugins mount their surfaces only once the shell asks, which happens in the frames that follow, so every mount crosses the bridge after this.
    // clang-format off
    const auto observer = [this](std::string_view name, const nlohmann::json& message, const nlohmann::json* reply) {
        if (reply != nullptr) {
            ++m_calls[std::string(name)];
        }

        if (const auto recorded = m_recorded.find(name); reply != nullptr && recorded != m_recorded.end()) {
            recorded->second = message;
        }

        m_declared.observe(name, message, reply);
    };
    // clang-format on

    m_product->runtime().observe(observer);

    // The notices of the start reach the shell as they do when the window opens.
    m_product->reportStartupNotices(m_time);

    // clang-format off
    if (!frameUntil([this]() { return m_product->shell().ready(); })) {
        return Result<void>::failure({"test_boot_timeout", "The plugins did not finish starting", m_data.string()});
    }
    // clang-format on

    return Result<void>::success();
}

void HeadlessProduct::frame() {
    m_product->pollScripts();
    std::ignore = m_product->service();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(width, height);
    io.DeltaTime = static_cast<float>(frameSeconds);
    m_time += frameSeconds;
    ImGui::NewFrame();
    m_product->draw(io.DisplaySize, m_time);
    ImGui::Render();
    ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
    m_demand = m_product->finishFrame();
}

// What the last frame asked of the frame loop, which tells whether the product would draw again or sleep.
const app::FrameDemand& HeadlessProduct::demand() const {
    return m_demand;
}

// Lua, the database thread and the timers all run on real time, so frames keep coming with a short pause until the condition holds.
bool HeadlessProduct::frameUntil(const std::function<bool()>& condition, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < deadline) {
        frame();

        if (condition()) {
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return false;
}

// Turns the loop as it turns while the window is minimized, advancing Lua and the components without drawing, until the condition holds.
bool HeadlessProduct::hiddenUntil(const std::function<bool()>& condition, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < deadline) {
        m_product->pollScripts();
        std::ignore = m_product->service();
        m_time += frameSeconds;
        std::ignore = m_product->updateHidden(m_time);

        if (condition()) {
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return false;
}

void HeadlessProduct::settle(std::chrono::milliseconds duration) {
    // clang-format off
    std::ignore = frameUntil([]() { return false; }, duration);
    // clang-format on
}

Result<void> HeadlessProduct::navigate(std::string_view destination) {
    return m_product->shell().navigate(std::string(destination));
}

ui::Surface* HeadlessProduct::surface(std::string_view id) {
    return m_product->surfaces().find(id);
}

// A surface the product unmounted on its own, such as one of a plugin turned off, shows nothing whatever its plugin last declared.
const DeclaredSurface* HeadlessProduct::declared(std::string_view id) const {
    return m_product->surfaces().contains(id) ? m_declared.find(id) : nullptr;
}

// Counts the calls of a host function the plugins made since the product started, answered or refused.
std::size_t HeadlessProduct::calls(std::string_view name) const {
    const auto found = m_calls.find(name);
    return found == m_calls.end() ? 0 : found->second;
}

// Keeps the message of the latest call of a host function from now on, which only the functions a test names pay for.
void HeadlessProduct::recordCalls(std::string_view name) {
    m_recorded.try_emplace(std::string(name), nlohmann::json());
}

nlohmann::json HeadlessProduct::lastCall(std::string_view name) const {
    const auto found = m_recorded.find(name);
    return found == m_recorded.end() ? nlohmann::json() : found->second;
}

// Lua numbers its nodes in the order it creates them, so the smallest identity of a kind is the first one the builder made.
std::optional<ui::NodeId> HeadlessProduct::firstNode(std::string_view surfaceId, std::string_view kind) {
    ui::Surface* mounted = surface(surfaceId);

    if (mounted == nullptr) {
        return std::nullopt;
    }

    std::optional<ui::NodeId> first;

    for (const auto& [id, component] : mounted->nodes) {
        if (component->kind() == kind && (!first.has_value() || id < *first)) {
            first = id;
        }
    }

    return first;
}

// An event names the properties it changed the way the component of its node names them, unless the test names them itself, so its node on the Lua side takes them.
void HeadlessProduct::emit(std::string_view surfaceId, ui::NodeId node, std::string_view name, nlohmann::json value, nlohmann::json state) {
    nlohmann::json order = nlohmann::json::array();

    if (state.empty()) {
        state = changedState(surfaceId, node, name, value, order);
    }

    nlohmann::json event = {{"surface", surfaceId}, {"node", node}, {"name", name}, {"value", std::move(value)}, {"state", std::move(state)}};

    if (!order.empty()) {
        event["order"] = std::move(order);
    }

    m_product->runtime().emit("workpane.ui.events", {{"events", nlohmann::json::array({std::move(event)})}});
}

// A choice reports its value or its check, a collection and a strip of tabs their selection, a splitter its ratio, a secret its reveal and a text view its zoom.
// A tab the reader dragged carries the items in their new order and the order of the pages that moved with them.
nlohmann::json HeadlessProduct::changedState(std::string_view surfaceId, ui::NodeId node, std::string_view name, nlohmann::json& value, nlohmann::json& order) {
    const ui::Surface* mounted = surface(surfaceId);
    const std::string kind = mounted != nullptr && mounted->nodes.contains(node) ? std::string(mounted->nodes.at(node)->kind()) : std::string();

    if (name == "change") {
        return value.contains("checked") ? nlohmann::json{{"checked", "checked"}} : nlohmann::json{{"value", "value"}};
    }

    if (name == "select" && kind == "tabs") {
        return {{"current", "id"}};
    }

    if (name == "select" && (kind == "list" || kind == "tree" || kind == "table")) {
        return {{"selected", "id"}};
    }

    for (const auto& [event, property] : {std::pair{"resize", "ratio"}, std::pair{"reveal", "revealed"}, std::pair{"zoom", "fontSize"}}) {
        if (name == event) {
            return {{property, property}};
        }
    }

    const DeclaredSurface* declaredSurface = declared(surfaceId);

    if (name != "move" || kind != "tabs" || declaredSurface == nullptr) {
        return nlohmann::json::object();
    }

    const DeclaredSurface::Node& tabs = declaredSurface->nodes.at(node);
    nlohmann::json items = tabs.properties.at("items");
    std::vector<ui::NodeId> pages = tabs.children;
    const auto index = value.at("index").get<std::size_t>();

    for (std::size_t from = 0; from < items.size(); ++from) {
        if (items[from].at("id") != value.at("id")) {
            continue;
        }

        nlohmann::json moved = items[from];
        items.erase(items.begin() + static_cast<std::ptrdiff_t>(from));
        items.insert(items.begin() + static_cast<std::ptrdiff_t>(index), std::move(moved));

        if (pages.size() == items.size()) {
            const ui::NodeId page = pages[from];
            pages.erase(pages.begin() + static_cast<std::ptrdiff_t>(from));
            pages.insert(pages.begin() + static_cast<std::ptrdiff_t>(index), page);
        }

        break;
    }

    value["items"] = std::move(items);
    order = pages;

    return {{"items", "items"}};
}

// The command modifier is Command on macOS and Control elsewhere, and ImGui reads a pressed Command as its control modifier on macOS.
ImGuiKeyChord HeadlessProduct::command() {
    return ImGui::GetIO().ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
}

// A combination is pressed in one frame and released in the next, which is how ImGui sees a real one.
void HeadlessProduct::press(ImGuiKeyChord chord) {
    ImGuiIO& io = ImGui::GetIO();
    const auto key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
    const std::array<ImGuiKey, 4> modifiers{ImGuiMod_Ctrl, ImGuiMod_Shift, ImGuiMod_Alt, ImGuiMod_Super};

    for (const bool down : {true, false}) {
        for (const ImGuiKey modifier : modifiers) {
            if ((chord & modifier) != 0) {
                io.AddKeyEvent(modifier, down);
            }
        }

        io.AddKeyEvent(key, down);
        frame();
    }
}

void HeadlessProduct::moveTo(ImVec2 point) {
    ImGui::GetIO().AddMousePosEvent(point.x, point.y);
    frame();
}

void HeadlessProduct::type(std::string_view text) {
    ImGui::GetIO().AddInputCharactersUTF8(std::string(text).c_str());
    frame();
}

// Tab moves the keyboard focus through the dialog on top until it reaches the button, and Space presses it, the way a keyboard user answers a dialog.
// A dialog is answered once it has opened and the named button has the keyboard, and the top dialog is read again at every step, because a plugin opens it from a task that may take a few frames and a dialog closing gives way to the next one.
bool HeadlessProduct::answerDialog(std::string_view button) {
    const std::string label = "##" + std::string(button);
    const auto deadline = std::chrono::steady_clock::now() + dialogTimeout;

    while (std::chrono::steady_clock::now() < deadline) {
        const ImGuiWindow* dialog = ImGui::GetTopMostPopupModal();

        if (dialog == nullptr) {
            frame();
            continue;
        }

        // A dialog whose keyboard focus rests on nothing, such as one a nested dialog left, is tabbed from its first item as it is after the reader used the mouse.
        if (GImGui->NavId == 0) {
            ImGui::SetNavCursorVisible(false);
        }

        // A button the dialog focused itself answers Space only once the keyboard cursor shows, as it does after the first key the reader presses.
        if (GImGui->NavId == ImHashStr(label.c_str(), 0, dialog->ID)) {
            ImGui::SetNavCursorVisible(true);
            press(ImGuiKey_Space);
            return true;
        }

        press(ImGuiKey_Tab);
    }

    return false;
}

std::vector<nlohmann::json> HeadlessProduct::query(std::string_view sql, const std::vector<nlohmann::json>& bindings) const {
    auto database = persistence::Database::open(m_data / persistence::DatabaseBootstrap::databaseName, true);

    if (!database.hasValue()) {
        return {};
    }

    auto rows = database.value().query(sql, bindings);
    return rows.hasValue() ? rows.value() : std::vector<nlohmann::json>{};
}

// A failed test prints the errors the product logged, which say why a state it waited for never came.
void HeadlessProduct::reportErrors() const {
    if (!::testing::Test::HasFailure()) {
        return;
    }

    for (const auto& row : query("SELECT source, category, message, details_json FROM logs__entries WHERE level = 'error'")) {
        std::cout << "Logged error: " << row.dump() << '\n';
    }
}

void HeadlessProduct::stop() {
    if (m_product != nullptr && !m_stopped) {
        m_product->stop();
        m_stopped = true;
    }
}

app::Product& HeadlessProduct::product() {
    return *m_product;
}

SystemRecord& HeadlessProduct::system() {
    return *m_system;
}

DialogRecord& HeadlessProduct::dialogs() {
    return *m_dialogs;
}

WebViewRecord& HeadlessProduct::webViews() {
    return *m_webViews;
}

TerminalRecord& HeadlessProduct::terminals() {
    return *m_terminals;
}

ProcessRecord& HeadlessProduct::processes() {
    return *m_processes;
}

AudioRecord& HeadlessProduct::audio() {
    return *m_audio;
}

} // namespace workpane::tests
