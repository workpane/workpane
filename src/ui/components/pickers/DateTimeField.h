#pragma once

#include "json/ObjectReader.h"
#include "time/LocalMoment.h"
#include "ui/model/Component.h"
#include "ui/model/NodeId.h"
#include "ui/model/RenderContext.h"
#include "ui/model/TextValue.h"

#include <imgui_internal.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace workpane::ui {

enum class MomentMode { Date, DateTime };

// A moment is written in the field and chosen in the calendar of this product, and choosing a day keeps the time already written.
class DateTimeField final : public Component {
  public:
    explicit DateTimeField(NodeId id);
    [[nodiscard]] std::string_view kind() const override;

  protected:
    void readProperties(json::ObjectReader& reader) override;
    [[nodiscard]] Restore keep() override;
    [[nodiscard]] FocusMode focusMode() const override;
    [[nodiscard]] ImVec2 measureContent(RenderContext& context, float availableWidth) override;
    void render(RenderContext& context, const ImRect& bounds) override;

  private:
    static constexpr float chevronWidth{9.0F};
    static constexpr float calendarMargin{8.0F};
    static constexpr float calendarSpacing{2.0F};
    static constexpr float calendarCellSize{34.0F};
    static constexpr int calendarWeeks{6};
    static constexpr int daysInWeek{7};

    [[nodiscard]] static std::optional<time::LocalMoment> parse(std::string_view text, MomentMode mode);
    [[nodiscard]] static std::string format(const time::LocalMoment& moment, MomentMode mode);
    [[nodiscard]] static bool number(std::string_view text, std::size_t offset, std::size_t length, int& out);
    [[nodiscard]] static int daysIn(int year, int month);
    [[nodiscard]] static int firstWeekday(int year, int month);
    [[nodiscard]] static std::string twoDigits(int value);

    void drawCalendar(RenderContext& context);
    void drawTime(RenderContext& context);
    void choose(RenderContext& context, const time::LocalMoment& moment);

    std::optional<time::LocalMoment> m_value;
    MomentMode m_mode{MomentMode::DateTime};
    TextValue m_placeholder;
    int m_shownYear{0};
    int m_shownMonth{1};
};

} // namespace workpane::ui
