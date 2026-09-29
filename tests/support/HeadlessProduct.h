#pragma once

#include "Result.h"
#include "app/FrameDemand.h"
#include "support/AudioRecord.h"
#include "support/DeclaredSurface.h"
#include "support/DeclaredSurfaces.h"
#include "support/DialogRecord.h"
#include "support/ProcessRecord.h"
#include "support/Resources.h"
#include "support/SystemRecord.h"
#include "support/TerminalRecord.h"
#include "support/WebViewRecord.h"
#include "ui/model/NodeId.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::app {
class Product;
}

namespace workpane::ui {
class Component;
struct Surface;
} // namespace workpane::ui

namespace workpane::tests {

// The whole product with its real SDK, plugins and database, drawn by ImGui without a window and answered by recording platform fakes.
// A wait for a condition allows thirty seconds, since a machine running the whole suite at once reaches slowly what a quiet one reaches at once.
class HeadlessProduct final {
  public:
    static constexpr double frameSeconds{1.0 / 60.0};

    static constexpr float width{1280.0F};
    static constexpr float height{832.0F};

    explicit HeadlessProduct(std::filesystem::path data, std::string locale = "en_US", std::filesystem::path resources = Resources::staged());
    ~HeadlessProduct();

    HeadlessProduct(const HeadlessProduct&) = delete;
    HeadlessProduct& operator=(const HeadlessProduct&) = delete;

    [[nodiscard]] Result<void> boot();
    void frame();
    [[nodiscard]] const app::FrameDemand& demand() const;
    [[nodiscard]] bool frameUntil(const std::function<bool()>& condition, std::chrono::milliseconds timeout = std::chrono::milliseconds(30000));
    [[nodiscard]] bool hiddenUntil(const std::function<bool()>& condition, std::chrono::milliseconds timeout = std::chrono::milliseconds(30000));
    void settle(std::chrono::milliseconds duration);
    [[nodiscard]] Result<void> navigate(std::string_view destination);
    [[nodiscard]] ui::Surface* surface(std::string_view id);
    [[nodiscard]] const DeclaredSurface* declared(std::string_view id) const;
    [[nodiscard]] std::size_t calls(std::string_view name) const;
    void recordCalls(std::string_view name);
    [[nodiscard]] nlohmann::json lastCall(std::string_view name) const;
    [[nodiscard]] std::optional<ui::NodeId> firstNode(std::string_view surface, std::string_view kind);
    void emit(std::string_view surface, ui::NodeId node, std::string_view name, nlohmann::json value, nlohmann::json state = nlohmann::json::object());
    [[nodiscard]] static ImGuiKeyChord command();
    void press(ImGuiKeyChord chord);
    void moveTo(ImVec2 point);
    void type(std::string_view text);
    [[nodiscard]] bool answerDialog(std::string_view button);
    [[nodiscard]] std::vector<nlohmann::json> query(std::string_view sql, const std::vector<nlohmann::json>& bindings = {}) const;
    void stop();

    [[nodiscard]] app::Product& product();
    [[nodiscard]] SystemRecord& system();
    [[nodiscard]] DialogRecord& dialogs();
    [[nodiscard]] WebViewRecord& webViews();
    [[nodiscard]] TerminalRecord& terminals();
    [[nodiscard]] ProcessRecord& processes();
    [[nodiscard]] AudioRecord& audio();

  private:
    static constexpr std::chrono::milliseconds dialogTimeout{5000};

    void reportErrors() const;
    [[nodiscard]] nlohmann::json changedState(std::string_view surface, ui::NodeId node, std::string_view name, nlohmann::json& value, nlohmann::json& order);

    std::filesystem::path m_data;
    std::filesystem::path m_resources;
    std::shared_ptr<SystemRecord> m_system{std::make_shared<SystemRecord>()};
    std::shared_ptr<DialogRecord> m_dialogs{std::make_shared<DialogRecord>()};
    std::shared_ptr<WebViewRecord> m_webViews{std::make_shared<WebViewRecord>()};
    std::shared_ptr<TerminalRecord> m_terminals{std::make_shared<TerminalRecord>()};
    std::shared_ptr<ProcessRecord> m_processes{std::make_shared<ProcessRecord>()};
    std::shared_ptr<AudioRecord> m_audio{std::make_shared<AudioRecord>()};
    DeclaredSurfaces m_declared;
    std::map<std::string, std::size_t, std::less<>> m_calls;
    std::map<std::string, nlohmann::json, std::less<>> m_recorded;
    std::unique_ptr<app::Product> m_product;
    app::FrameDemand m_demand;
    double m_time{0.0};
    bool m_imguiReady{false};
    bool m_stopped{false};
};

} // namespace workpane::tests
