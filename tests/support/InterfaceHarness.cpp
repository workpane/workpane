#include "support/InterfaceHarness.h"

#include "execution/MainThreadQueue.h"
#include "execution/WorkerPool.h"
#include "localization/CoreCatalog.h"
#include "localization/Localization.h"
#include "support/Resources.h"
#include "ui/FontFamilies.h"
#include "ui/Fonts.h"
#include "ui/TextureCache.h"
#include "ui/components/StandardComponents.h"
#include "ui/model/Component.h"
#include "ui/model/ComponentRegistry.h"
#include "ui/model/RenderContext.h"
#include "ui/model/Surface.h"
#include "ui/model/SurfaceStore.h"
#include "ui/theme/Style.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <imgui_impl_null.h>
#include <imgui_internal.h>

#include <array>
#include <stdexcept>
#include <string>
#include <tuple>

namespace workpane::tests {

InterfaceHarness::InterfaceHarness() {
    m_localization = std::make_unique<localization::Localization>();
    std::ignore = m_localization->registerCatalog(localization::Localization::coreOwner, localization::CoreCatalog::catalog());
    std::ignore = m_localization->selectLanguage("en");
    m_themes = std::make_unique<ui::ThemeManager>();
    m_mainThread = std::make_unique<execution::MainThreadQueue>();
    m_workers = std::make_unique<execution::WorkerPool>(1);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplNullPlatform_Init();
    ImGui_ImplNullRender_Init();

    m_fonts = std::make_unique<ui::Fonts>();

    if (const auto loaded = m_fonts->load(*io.Fonts, Resources::fonts()); !loaded.hasValue()) {
        throw std::runtime_error(loaded.error().message + ": " + loaded.error().detail);
    }

    m_families = std::make_unique<ui::FontFamilies>(*m_fonts, *io.Fonts, m_system, *m_workers, *m_mainThread);
    m_textures = std::make_unique<ui::TextureCache>(*m_workers, *m_mainThread);
    m_events = std::make_unique<ui::EventSink>();
    m_nativeViews = std::make_unique<FakeNativeViewHost>(m_webViews);
    m_terminals = std::make_unique<FakePseudoTerminalHost>(m_terminalRecord);
    m_context = std::make_unique<ui::RenderContext>(m_themes->theme(), *m_fonts, *m_families, *m_localization, *m_textures, *m_events, *m_nativeViews, *m_terminals, 1.0F);
    m_registry = std::make_unique<ui::ComponentRegistry>();
    ui::StandardComponents::registerAll(*m_registry);
    m_surfaces = std::make_unique<ui::SurfaceStore>(*m_registry);
    ui::Style::apply(m_themes->theme(), *m_fonts, 1.0F, ImGui::GetStyle());
}

InterfaceHarness::~InterfaceHarness() {
    m_surfaces->unmountAll(*m_context);
    m_textures->release();
    m_workers->shutdown();
    m_mainThread->drain();
    ImGui_ImplNullRender_Shutdown();
    ImGui_ImplNullPlatform_Shutdown();
    ImGui::DestroyContext();
}

Result<void> InterfaceHarness::mount(const nlohmann::json& tree) {
    return m_surfaces->mount(surface, "test", Resources::staged(), tree, *m_context);
}

Result<void> InterfaceHarness::patch(ui::NodeId node, const nlohmann::json& properties) {
    return m_surfaces->patch(surface, "test", node, properties);
}

ui::Component* InterfaceHarness::node(ui::NodeId id) {
    ui::Surface* mounted = m_surfaces->find(surface);

    if (mounted == nullptr || !mounted->nodes.contains(id)) {
        return nullptr;
    }

    return mounted->nodes.at(id);
}

// One frame of the product: the surface fills the display inside one borderless window, and the renderer settles the textures it asked for.
void InterfaceHarness::frame() {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(width, height);
    io.DeltaTime = static_cast<float>(frameSeconds);
    m_time += frameSeconds;
    ImGui::NewFrame();
    ui::Style::matchDensity(io.DisplayFramebufferScale.x);
    m_textures->update();
    m_context->beginFrame(m_time);
    m_context->resetFrameRequest();
    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##harness", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse);

    if (ui::Surface* mounted = m_surfaces->find(surface); mounted != nullptr) {
        m_context->setSurface(mounted->id, mounted->assets);
        mounted->root->draw(*m_context, ImRect(ImVec2(0.0F, 0.0F), io.DisplaySize));
    }

    ImGui::End();
    m_surfaces->update(*m_context);
    ImGui::Render();
    ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
    m_mainThread->drain();
    collectEvents();
}

void InterfaceHarness::collectEvents() {
    for (auto& event : m_events->take()) {
        m_taken.push_back(std::move(event));
    }
}

void InterfaceHarness::frames(int count) {
    for (int index = 0; index < count; ++index) {
        frame();
    }
}

void InterfaceHarness::moveTo(ImVec2 point) {
    ImGui::GetIO().AddMousePosEvent(point.x, point.y);
    frame();
}

// A click is a press and a release in separate frames, which is how ImGui sees a real one.
void InterfaceHarness::click(ImVec2 point) {
    moveTo(point);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
}

// A drag presses at one point, moves in small steps the way a hand does and releases at the other.
void InterfaceHarness::drag(ImVec2 from, ImVec2 to) {
    moveTo(from);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();

    for (int step = 1; step <= dragSteps; ++step) {
        const float progress = static_cast<float>(step) / static_cast<float>(dragSteps);
        moveTo(ImVec2(from.x + (to.x - from.x) * progress, from.y + (to.y - from.y) * progress));
    }

    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
}

void InterfaceHarness::rightClick(ImVec2 point) {
    moveTo(point);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Right, true);
    frame();
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Right, false);
    frame();
}

void InterfaceHarness::type(std::string_view text) {
    const std::string characters(text);
    ImGui::GetIO().AddInputCharactersUTF8(characters.c_str());
    frame();
}

// The command modifier is Command on macOS and Control elsewhere, and ImGui reads a pressed Command as its control modifier on macOS.
ImGuiKeyChord InterfaceHarness::command() {
    return ImGui::GetIO().ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
}

// A combination is pressed in one frame and released in the next, which is how ImGui sees a real one.
void InterfaceHarness::press(ImGuiKeyChord chord) {
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

std::vector<ui::UiEvent> InterfaceHarness::takeEvents() {
    collectEvents();

    return std::exchange(m_taken, {});
}

ui::RenderContext& InterfaceHarness::context() {
    return *m_context;
}

ui::SurfaceStore& InterfaceHarness::surfaces() {
    return *m_surfaces;
}

ui::ComponentRegistry& InterfaceHarness::registry() {
    return *m_registry;
}

localization::Localization& InterfaceHarness::localization() {
    return *m_localization;
}

ui::ThemeManager& InterfaceHarness::themes() {
    return *m_themes;
}

WebViewRecord& InterfaceHarness::webViews() {
    return *m_webViews;
}

TerminalRecord& InterfaceHarness::terminals() {
    return *m_terminalRecord;
}

SystemRecord& InterfaceHarness::system() {
    return *m_systemRecord;
}

} // namespace workpane::tests
