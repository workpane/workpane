#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/Color.h"
#include "ui/WidgetHelper.h"
#include "ui/model/CommonProperties.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/model/TextValue.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"

#include <imgui_internal.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace workpane::ui {

// A surface its plugin draws on with lists of shapes, pictures and text, ticking at a frame rate and reporting the pointer and the keys, which is what games and drawings are made of.
class Canvas final : public Component {
  public:
    explicit Canvas(NodeId id);
    [[nodiscard]] std::string_view kind() const override;
    [[nodiscard]] Result<void> command(RenderContext& context, std::string_view name, const json::Json& arguments) override;
    void detach(RenderContext& context) override;
    void update(RenderContext& context) override;

  protected:
    [[nodiscard]] Alignment defaultRowAlignment() const override;
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] Result<void> validate() const override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    static constexpr float defaultHeight{200.0F};
    static constexpr double largestCoordinate{100000.0};
    static constexpr std::size_t largestList{20000};
    static constexpr std::size_t mostPictures{256};
    static constexpr std::int64_t fastestRate{120};
    static constexpr double longestTick{0.25};
    static constexpr double smallestText{6.0};
    static constexpr double largestText{128.0};
    static constexpr double smallestTile{1.0};
    static constexpr double mostTiles{4096.0};
    static constexpr float nearestInset{1.0F / 64.0F};
    static constexpr float linearInset{0.5F};

    enum class Operation { Rectangle, Line, Circle, Image, Text, Clip, Unclip };

    struct Paint final {
        std::optional<ThemeColor> role;
        Color literal;
        float opacity{1.0F};
    };

    struct Command final {
        Operation operation{Operation::Rectangle};
        ImVec2 position;
        ImVec2 size;
        ImVec2 end;
        float radius{0.0F};
        float thickness{1.0F};
        float rotation{0.0F};
        bool filled{true};
        bool flipX{false};
        bool flipY{false};
        std::optional<ImRect> source;
        std::optional<ImVec2> tile;
        std::string image;
        TextValue text;
        FontFace face{FontFace::Regular};
        float textSize{12.0F};
        TextAlign align{TextAlign::Start};
        Paint paint;
    };

    [[nodiscard]] static Result<Command> parse(const json::Json& entry, std::size_t index);
    [[nodiscard]] static Result<Paint> paint(const std::string& color, double opacity, const std::string& place);
    [[nodiscard]] static Color colorOf(const RenderContext& context, const Paint& paint);

    void tick(RenderContext& context, const ImRect& bounds);
    void focus(RenderContext& context, ImGuiID item, const ImRect& bounds);
    void pointer(RenderContext& context, const ImRect& bounds);
    void keys(RenderContext& context);
    void paintCommands(RenderContext& context, const ImRect& bounds);
    void drawText(RenderContext& context, ImDrawList& list, const ImVec2& at, const Command& command);
    void drawImage(RenderContext& context, ImDrawList& list, const ImVec2& origin, const Command& command, bool nearest);

    std::vector<Command> m_commands;
    std::int64_t m_frameRate{0};
    bool m_pixelated{false};
    bool m_focusable{false};
    bool m_tracking{false};
    std::optional<Paint> m_background;
    double m_lastTick{-1.0};
    double m_nextTick{0.0};
    ImVec2 m_size;
    ImVec2 m_pointer{-1.0F, -1.0F};
    bool m_focused{false};
    bool m_focusWanted{false};
    std::vector<std::string> m_held;
    std::vector<std::string> m_failedImages;
    std::vector<std::string> m_pictures;
    std::vector<std::filesystem::path> m_holding;
    bool m_picturesChanged{false};
};

} // namespace workpane::ui
