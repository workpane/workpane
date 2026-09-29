#pragma once

#include "Result.h"
#include "support/FakeNativeViewHost.h"
#include "support/FakePseudoTerminalHost.h"
#include "support/FakeSystemServices.h"
#include "support/SystemRecord.h"
#include "support/TerminalRecord.h"
#include "support/WebViewRecord.h"
#include "ui/model/EventSink.h"
#include "ui/model/NodeId.h"
#include "ui/model/UiEvent.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <memory>
#include <string_view>
#include <vector>

namespace workpane::execution {
class MainThreadQueue;
class WorkerPool;
} // namespace workpane::execution

namespace workpane::localization {
class Localization;
}

namespace workpane::ui {
class Component;
class ComponentRegistry;
class FontFamilies;
class Fonts;
class RenderContext;
class SurfaceStore;
class TextureCache;
class ThemeManager;
} // namespace workpane::ui

namespace workpane::tests {

// A headless ImGui frame with the fonts, themes, catalogs and components of the product, which draws one mounted surface over the whole display.
class InterfaceHarness final {
  public:
    static constexpr float width{1280.0F};
    static constexpr float height{832.0F};
    static constexpr const char* surface{"view:test:page"};

    InterfaceHarness();
    ~InterfaceHarness();

    InterfaceHarness(const InterfaceHarness&) = delete;
    InterfaceHarness& operator=(const InterfaceHarness&) = delete;

    [[nodiscard]] Result<void> mount(const nlohmann::json& tree);
    [[nodiscard]] Result<void> patch(ui::NodeId node, const nlohmann::json& properties);
    [[nodiscard]] ui::Component* node(ui::NodeId id);
    void frame();
    void frames(int count);
    void moveTo(ImVec2 point);
    void click(ImVec2 point);
    void rightClick(ImVec2 point);
    void drag(ImVec2 from, ImVec2 to);
    void type(std::string_view text);
    [[nodiscard]] static ImGuiKeyChord command();
    void press(ImGuiKeyChord chord);
    [[nodiscard]] std::vector<ui::UiEvent> takeEvents();

    [[nodiscard]] ui::RenderContext& context();
    [[nodiscard]] ui::SurfaceStore& surfaces();
    [[nodiscard]] ui::ComponentRegistry& registry();
    [[nodiscard]] localization::Localization& localization();
    [[nodiscard]] ui::ThemeManager& themes();
    [[nodiscard]] WebViewRecord& webViews();
    [[nodiscard]] TerminalRecord& terminals();
    [[nodiscard]] SystemRecord& system();

  private:
    static constexpr double frameSeconds{1.0 / 60.0};
    static constexpr int dragSteps{8};

    void collectEvents();

    std::shared_ptr<WebViewRecord> m_webViews{std::make_shared<WebViewRecord>()};
    std::shared_ptr<TerminalRecord> m_terminalRecord{std::make_shared<TerminalRecord>()};
    std::shared_ptr<SystemRecord> m_systemRecord{std::make_shared<SystemRecord>()};
    FakeSystemServices m_system{m_systemRecord};
    std::unique_ptr<localization::Localization> m_localization;
    std::unique_ptr<ui::ThemeManager> m_themes;
    std::unique_ptr<execution::MainThreadQueue> m_mainThread;
    std::unique_ptr<execution::WorkerPool> m_workers;
    std::unique_ptr<ui::Fonts> m_fonts;
    std::unique_ptr<ui::FontFamilies> m_families;
    std::unique_ptr<ui::TextureCache> m_textures;
    std::unique_ptr<ui::EventSink> m_events;
    std::unique_ptr<FakeNativeViewHost> m_nativeViews;
    std::unique_ptr<FakePseudoTerminalHost> m_terminals;
    std::unique_ptr<ui::RenderContext> m_context;
    std::unique_ptr<ui::ComponentRegistry> m_registry;
    std::unique_ptr<ui::SurfaceStore> m_surfaces;
    std::vector<ui::UiEvent> m_taken;
    double m_time{0.0};
};

} // namespace workpane::tests
