#pragma once

#include "Result.h"
#include "json/ObjectReader.h"
#include "ui/IconCatalog.h"
#include "ui/model/CommonProperties.h"
#include "ui/model/Insets.h"
#include "ui/model/NodeId.h"
#include "ui/model/TextValue.h"
#include "ui/theme/Theme.h"

#include <imgui_internal.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace workpane::ui {

// How a control takes the keyboard when asked: a typing control starts editing, and a choosing control only receives the navigation cursor, so Space opens it.
enum class FocusMode { None, Typing, Choosing };

class RenderContext;

// A node of a retained tree: Lua declares its properties, and it measures, draws and reports its events on its own.
// A patch is read into the properties of the node itself, proven against all of them and against its children, and put back whole when any check refuses it.
class Component {
  public:
    static constexpr std::size_t unlimitedChildren{std::numeric_limits<std::size_t>::max()};

    explicit Component(NodeId id);
    virtual ~Component() = default;

    Component(const Component&) = delete;
    Component& operator=(const Component&) = delete;

    [[nodiscard]] NodeId id() const;
    [[nodiscard]] virtual std::string_view kind() const = 0;
    [[nodiscard]] const CommonProperties& common() const;
    [[nodiscard]] Alignment columnAlignment() const;
    [[nodiscard]] Alignment rowAlignment() const;
    [[nodiscard]] virtual std::size_t childLimit() const;
    [[nodiscard]] std::vector<std::unique_ptr<Component>>& children();
    [[nodiscard]] const std::vector<std::unique_ptr<Component>>& children() const;

    [[nodiscard]] Result<void> apply(const json::Json& properties);
    [[nodiscard]] Result<void> patch(const json::Json& properties);
    [[nodiscard]] virtual Result<void> validateChildren() const;
    [[nodiscard]] ImVec2 measure(RenderContext& context, float availableWidth);
    [[nodiscard]] float boundedWidth(float width, float scale) const;
    [[nodiscard]] float boundedHeight(float height, float scale) const;
    void draw(RenderContext& context, const ImRect& bounds);
    [[nodiscard]] virtual Result<void> command(RenderContext& context, std::string_view name, const json::Json& arguments);
    virtual void detach(RenderContext& context);
    virtual void update(RenderContext& context);

  protected:
    using Restore = std::function<void()>;

    // clang-format off
    template <typename... Fields> [[nodiscard]] static Restore kept(Fields&... fields) {
        return [&fields..., saved = std::tuple<Fields...>(fields...)]() mutable { std::tie(fields...) = std::move(saved); };
    }
    // clang-format on

    [[nodiscard]] static ImRect snapped(float x, float y, float width, float height);
    [[nodiscard]] static float alignedOffset(Alignment alignment, float available, float size);

    [[nodiscard]] virtual Alignment defaultColumnAlignment() const;
    [[nodiscard]] virtual Alignment defaultRowAlignment() const;
    [[nodiscard]] virtual FocusMode focusMode() const;
    [[nodiscard]] virtual bool revealing() const;
    virtual void readProperties(json::ObjectReader& reader) = 0;
    [[nodiscard]] virtual Restore keep() = 0;
    [[nodiscard]] virtual Result<void> validate() const;
    virtual void applied();
    [[nodiscard]] virtual ImVec2 measureContent(RenderContext& context, float availableWidth) = 0;
    virtual void render(RenderContext& context, const ImRect& bounds) = 0;

    void readText(json::ObjectReader& reader, std::string_view key, TextValue& out);
    void readInsets(json::ObjectReader& reader, std::string_view key, Insets& out);
    void readColor(json::ObjectReader& reader, std::string_view key, std::optional<ThemeColor>& out);
    void readIcon(json::ObjectReader& reader, std::string_view key, std::optional<Icon>& out);
    void readSpacing(json::ObjectReader& reader, std::string_view key, float& out);
    [[nodiscard]] std::vector<Component*> visibleChildren() const;
    void fail(Error error);

  private:
    static constexpr double tooltipDelaySeconds{0.5};
    static constexpr double largestLength{100000.0};
    static constexpr double largestInset{1000.0};
    static constexpr double largestGrow{1000.0};
    static constexpr std::int64_t largestCollapse{100};

    static void readLength(json::ObjectReader& reader, std::string_view key, std::optional<float>& out, std::optional<Error>& failure, std::string_view kind);
    static void readBound(json::ObjectReader& reader, std::string_view key, float& out);
    [[nodiscard]] static float scaled(float value, float scale);

    [[nodiscard]] Result<void> read(const json::Json& properties);
    void readCommon(json::ObjectReader& reader);
    void readDrag(json::ObjectReader& reader);
    void drawTooltip(RenderContext& context, const ImRect& bounds);
    void drawMenu(RenderContext& context, const ImRect& bounds);
    void drawDrag(RenderContext& context, const ImRect& bounds);
    void drawDrop(RenderContext& context, const ImRect& bounds);

    NodeId m_id;
    CommonProperties m_common;
    std::vector<std::unique_ptr<Component>> m_children;
    std::optional<Error> m_readFailure;
    double m_hoverStarted{-1.0};
    std::uint64_t m_measuredFrame{0};
    float m_measuredWidth{-1.0F};
    ImVec2 m_measuredSize;
    bool m_focusRequested{false};
};

} // namespace workpane::ui
