#pragma once

#include "Result.h"
#include "ui/shell/BandItem.h"
#include "ui/shell/DialogHost.h"
#include "ui/shell/ModeBar.h"
#include "ui/shell/ModeEntry.h"
#include "ui/shell/NavigationItem.h"
#include "ui/shell/SettingsGroup.h"
#include "ui/shell/SettingsView.h"
#include "ui/shell/ToastOverlay.h"

#include <imgui_internal.h>

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace workpane::ui {

class RenderContext;
class SurfaceStore;

// The whole window: the bands of the plugins across its top and its bottom, the mode bar, the destination it selects, the notifications above it and the dialogs above everything.
class Shell final {
  public:
    static constexpr const char* settingsDestination{"workpane:settings"};

    using ViewRequest = std::function<void(const NavigationItem& item)>;
    using BandRequest = std::function<void(const BandItem& item)>;
    using QuitRequest = std::function<void()>;
    using ShortcutRequest = std::function<void(const NavigationItem& item, const std::string& shortcut)>;

    Shell(ViewRequest viewRequest, BandRequest bandRequest, SettingsView::SectionRequest sectionRequest, QuitRequest quitRequest, ShortcutRequest shortcutRequest);

    void setNavigation(std::vector<NavigationItem> items);
    void setBands(std::vector<BandItem> bands);
    void setSettingsGroups(std::vector<SettingsGroup> groups);
    void setReady(bool ready);
    [[nodiscard]] bool ready() const;
    [[nodiscard]] Result<void> navigate(const std::string& destination);
    [[nodiscard]] const std::string& destination() const;
    [[nodiscard]] const std::vector<NavigationItem>& navigation() const;
    [[nodiscard]] const std::vector<BandItem>& bands() const;
    void markFailed(const std::string& surface);
    [[nodiscard]] ToastOverlay& toasts();
    [[nodiscard]] DialogHost& dialogs();
    [[nodiscard]] SettingsView& settings();
    [[nodiscard]] static std::vector<ImRect> floatingWindows();
    void draw(RenderContext& context, SurfaceStore& surfaces, ImVec2 windowSize);

  private:
    static constexpr float loadingIndicatorSize{28.0F};
    static constexpr float settledTolerance{0.5F};
    static constexpr const char* windowName{"##workpane"};

    static void drawDrag(const RenderContext& context);
    static void drawComposition(const RenderContext& context);
    static void releaseVanishedFocus();
    [[nodiscard]] static bool settling();
    [[nodiscard]] static bool corePressed(ImGuiKeyChord chord);

    void drawLoading(RenderContext& context, ImVec2 windowSize);
    [[nodiscard]] ImRect drawBands(RenderContext& context, SurfaceStore& surfaces, ImVec2 windowSize);
    void drawSurface(RenderContext& context, SurfaceStore& surfaces, const ImRect& bounds, const std::string& surfaceId, const std::function<void()>& request);
    void forgetVanishedSurfaces();
    void requestPreloads();
    void handleShortcuts(const std::vector<ModeEntry>& entries);
    [[nodiscard]] std::vector<ModeEntry> entries(RenderContext& context) const;

    ViewRequest m_viewRequest;
    BandRequest m_bandRequest;
    SettingsView::SectionRequest m_sectionRequest;
    QuitRequest m_quitRequest;
    ShortcutRequest m_shortcutRequest;
    std::vector<NavigationItem> m_navigation;
    std::vector<BandItem> m_bands;
    ModeBar m_modeBar;
    SettingsView m_settings;
    ToastOverlay m_toasts;
    DialogHost m_dialogs;
    std::string m_destination;
    std::set<std::string, std::less<>> m_requested;
    std::set<std::string, std::less<>> m_failed;
    float m_contentBottom{0.0F};
    bool m_ready{false};
};

} // namespace workpane::ui
