#pragma once

#include "ui/Color.h"
#include "ui/Fonts.h"
#include "ui/NativeWebView.h"
#include "ui/ParagraphCache.h"
#include "ui/model/DragValue.h"
#include "ui/model/EventSink.h"
#include "ui/model/NodeId.h"
#include "ui/model/TextValue.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"

#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::localization {
class Localization;
}

namespace workpane::ui {

class FontFamilies;
class TextureCache;
class NativeViewHost;
class PseudoTerminalHost;

// Everything a component reads while it measures and draws, so no component reaches a global or another component.
class RenderContext final {
  public:
    RenderContext(const Theme& theme, const Fonts& fonts, FontFamilies& families, const localization::Localization& localization, TextureCache& textures, EventSink& events, NativeViewHost& nativeViews, PseudoTerminalHost& terminals, float scale);

    [[nodiscard]] const Theme& theme() const;
    [[nodiscard]] const Fonts& fonts() const;
    [[nodiscard]] FontFamilies& families() const;
    [[nodiscard]] const localization::Localization& localization() const;
    [[nodiscard]] TextureCache& textures() const;
    [[nodiscard]] NativeViewHost& nativeViews() const;
    [[nodiscard]] PseudoTerminalHost& terminals() const;
    [[nodiscard]] float scale() const;
    [[nodiscard]] float metric(ThemeMetric role) const;
    [[nodiscard]] Color color(ThemeColor role) const;
    [[nodiscard]] FontRole font(ThemeFont role) const;
    [[nodiscard]] const std::string& text(const TextValue& value) const;
    [[nodiscard]] std::string translate(std::string_view key) const;
    void setTheme(const Theme& theme);
    void setScale(float scale);

    void setSurface(std::string surface, std::filesystem::path assets);
    [[nodiscard]] const std::string& surface() const;
    [[nodiscard]] const std::filesystem::path& assets() const;
    void emit(NodeId node, std::string name, nlohmann::json value = nlohmann::json::object());
    void emit(NodeId node, std::string name, nlohmann::json value, nlohmann::json state, std::vector<NodeId> order = {});
    void report(NodeId node, std::string name, nlohmann::json value);

    void beginFrame(double seconds);
    [[nodiscard]] std::uint64_t frame() const;
    [[nodiscard]] double time() const;
    void requestFrame();
    void requestFrameAt(double seconds);
    [[nodiscard]] bool frameRequested() const;
    [[nodiscard]] double frameDeadline() const;
    void resetFrameRequest();

    [[nodiscard]] bool claimMenuClick();
    [[nodiscard]] bool claimActivation();
    void dropFiles(std::vector<std::filesystem::path> paths, ImVec2 position);
    [[nodiscard]] std::optional<std::vector<std::filesystem::path>> takeDroppedFiles(const ImRect& bounds);
    void pressDrag(NodeId node, const DragValue& value);
    [[nodiscard]] bool dragPressed(NodeId node) const;
    [[nodiscard]] bool dragSource(NodeId node) const;
    void moveDrag();
    [[nodiscard]] const DragValue* dragging() const;
    [[nodiscard]] bool claimDropTarget();

    void setModalActive(bool active);
    [[nodiscard]] bool modalActive() const;

    [[nodiscard]] std::uint64_t keepPopup(std::unique_ptr<NativeWebView> popup);
    [[nodiscard]] std::unique_ptr<NativeWebView> takePopup(std::uint64_t popup);

    void compose(std::string text, std::size_t caret);
    [[nodiscard]] const std::string& composition() const;
    [[nodiscard]] std::size_t compositionCaret() const;

    [[nodiscard]] ParagraphCache& paragraphs() const;

  private:
    static constexpr std::uint64_t popupFrames{8};

    struct KeptPopup final {
        std::unique_ptr<NativeWebView> view;
        std::uint64_t frame;
    };

    struct DragSession final {
        std::string surface;
        NodeId source;
        DragValue value;
        std::uint64_t frame;
        bool moving;
    };

    const Theme* m_theme;
    const Fonts& m_fonts;
    FontFamilies& m_families;
    const localization::Localization& m_localization;
    TextureCache& m_textures;
    EventSink& m_events;
    NativeViewHost& m_nativeViews;
    PseudoTerminalHost& m_terminals;
    float m_scale{1.0F};
    std::string m_surface;
    std::filesystem::path m_assets;
    double m_time{0.0};
    std::uint64_t m_frame{1};
    mutable ParagraphCache m_paragraphs;
    bool m_frameRequested{false};
    double m_frameDeadline{std::numeric_limits<double>::infinity()};
    bool m_modalActive{false};
    bool m_menuClaimed{false};
    bool m_activationClaimed{false};
    std::optional<std::vector<std::filesystem::path>> m_droppedFiles;
    ImVec2 m_filesPosition;
    std::uint64_t m_filesFrame{0};
    std::optional<DragSession> m_drag;
    std::uint64_t m_dropClaimFrame{0};
    std::map<std::uint64_t, KeptPopup> m_popups;
    std::uint64_t m_nextPopup{0};
    std::string m_composition;
    std::size_t m_compositionCaret{0};
};

} // namespace workpane::ui
