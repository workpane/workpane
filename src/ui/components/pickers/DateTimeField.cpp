#include "ui/components/pickers/DateTimeField.h"

#include "localization/Localization.h"
#include "time/TimestampHelper.h"
#include "ui/ButtonState.h"
#include "ui/IconCatalog.h"
#include "ui/Painter.h"
#include "ui/WidgetHelper.h"
#include "ui/theme/FontRole.h"
#include "ui/theme/Theme.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace workpane::ui {

bool DateTimeField::number(std::string_view text, std::size_t offset, std::size_t length, int& out) {
    const std::string_view digits = text.substr(offset, length);

    if (digits.size() != length) {
        return false;
    }

    for (const char digit : digits) {
        if (digit < '0' || digit > '9') {
            return false;
        }
    }

    const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), out);
    return result.ec == std::errc{};
}

int DateTimeField::daysIn(int year, int month) {
    const std::chrono::year_month_day_last last{std::chrono::year(year), std::chrono::month_day_last(std::chrono::month(static_cast<unsigned>(month)))};
    return static_cast<int>(static_cast<unsigned>(last.day()));
}

// The week starts on Sunday, which is how both supported languages print a calendar.
int DateTimeField::firstWeekday(int year, int month) {
    const std::chrono::sys_days first = std::chrono::year_month_day{std::chrono::year(year), std::chrono::month(static_cast<unsigned>(month)), std::chrono::day(1)};
    return static_cast<int>(std::chrono::weekday(first).c_encoding());
}

std::string DateTimeField::twoDigits(int value) {
    std::array<char, 8> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%02d", value);

    return buffer.data();
}

DateTimeField::DateTimeField(NodeId id) : Component(id) {}

std::string_view DateTimeField::kind() const {
    return "dateTimeField";
}

std::optional<time::LocalMoment> DateTimeField::parse(std::string_view text, MomentMode mode) {
    const std::size_t expected = mode == MomentMode::Date ? 10 : 16;

    if (text.size() != expected || text[4] != '-' || text[7] != '-' || (mode == MomentMode::DateTime && (text[10] != ' ' || text[13] != ':'))) {
        return std::nullopt;
    }

    time::LocalMoment moment;

    if (!number(text, 0, 4, moment.year) || !number(text, 5, 2, moment.month) || !number(text, 8, 2, moment.day)) {
        return std::nullopt;
    }

    if (mode == MomentMode::DateTime && (!number(text, 11, 2, moment.hour) || !number(text, 14, 2, moment.minute))) {
        return std::nullopt;
    }

    const std::chrono::year_month_day date{std::chrono::year(moment.year), std::chrono::month(static_cast<unsigned>(moment.month)), std::chrono::day(static_cast<unsigned>(moment.day))};

    if (!date.ok() || moment.hour > 23 || moment.minute > 59) {
        return std::nullopt;
    }

    return moment;
}

std::string DateTimeField::format(const time::LocalMoment& moment, MomentMode mode) {
    std::array<char, 32> buffer{};

    if (mode == MomentMode::Date) {
        std::snprintf(buffer.data(), buffer.size(), "%04d-%02d-%02d", moment.year, moment.month, moment.day);
    } else {
        std::snprintf(buffer.data(), buffer.size(), "%04d-%02d-%02d %02d:%02d", moment.year, moment.month, moment.day, moment.hour, moment.minute);
    }

    return buffer.data();
}

FocusMode DateTimeField::focusMode() const {
    return FocusMode::Choosing;
}

void DateTimeField::readProperties(json::ObjectReader& reader) {
    reader.readChoice("mode", m_mode, {{"date", MomentMode::Date}, {"datetime", MomentMode::DateTime}}, json::Presence::Optional);
    readText(reader, "placeholder", m_placeholder);

    if (!reader.contains("value")) {
        return;
    }

    std::string value;
    reader.read("value", value);

    if (value.empty()) {
        m_value.reset();
        return;
    }

    m_value = parse(value, m_mode);

    if (!m_value.has_value()) {
        fail({"ui_moment_invalid", "A moment is not written as its mode declares", value});
    }
}

Component::Restore DateTimeField::keep() {
    return kept(m_mode, m_placeholder, m_value);
}

ImVec2 DateTimeField::measureContent(RenderContext& context, float availableWidth) {
    return {std::min(availableWidth, context.metric(ThemeMetric::SettingsControlMinimumWidth)), WidgetHelper::controlHeight(context)};
}

void DateTimeField::render(RenderContext& context, const ImRect& bounds) {
    const ButtonState state = WidgetHelper::interact(ImGui::GetID("##moment"), bounds);
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const float padding = context.metric(ThemeMetric::ControlHorizontalPadding);
    const float indicator = context.metric(ThemeMetric::ComboIndicatorWidth);
    const bool open = ImGui::IsPopupOpen("##calendar");

    WidgetHelper::frame(context, bounds, context.color(ThemeColor::Raised));
    const std::string text = m_value.has_value() ? format(*m_value, m_mode) : context.text(m_placeholder);
    WidgetHelper::alignedText(context, list, context.font(ThemeFont::Interface), ImRect(bounds.Min.x + padding, bounds.Min.y, bounds.Max.x - indicator, bounds.Max.y), context.color(m_value.has_value() && !WidgetHelper::disabled() ? ThemeColor::Text : ThemeColor::TextMuted), text, TextAlign::Start);
    Painter::chevron(list, ImVec2(bounds.Max.x - indicator / 2.0F, bounds.GetCenter().y), chevronWidth * context.scale(), ChevronDirection::Down, context.color(WidgetHelper::disabled() ? ThemeColor::TextMuted : ThemeColor::Text));

    if (open) {
        WidgetHelper::focusBorder(context, bounds);
    }

    const std::optional<time::LocalMoment> shown = !state.pressed || m_value.has_value() ? m_value : time::TimestampHelper::localMoment(time::TimestampHelper::now());

    if (state.pressed && shown.has_value()) {
        m_shownYear = shown->year;
        m_shownMonth = shown->month;
        ImGui::OpenPopup("##calendar");
    }

    const float margin = calendarMargin * context.scale();
    WidgetHelper::placePopup(context, "##calendar", bounds);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(margin, margin));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0F, 0.0F));

    if (ImGui::BeginPopup("##calendar", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize)) {
        drawCalendar(context);

        if (m_mode == MomentMode::DateTime) {
            drawTime(context);
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);
}

void DateTimeField::drawCalendar(RenderContext& context) {
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const float scale = context.scale();
    const float cell = calendarCellSize * scale;
    const float spacing = calendarSpacing * scale;
    const float width = cell * daysInWeek + spacing * (daysInWeek - 1);
    const float header = context.metric(ThemeMetric::CompactButtonSize);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const FontRole font = context.font(ThemeFont::Interface);

    if (WidgetHelper::button(context, "##previous", ImRect(origin, ImVec2(origin.x + header, origin.y + header)), {}, Icon::Back, ButtonVariant::Icon)) {
        m_shownMonth = m_shownMonth == 1 ? 12 : m_shownMonth - 1;
        m_shownYear = m_shownMonth == 12 ? m_shownYear - 1 : m_shownYear;
    }

    if (WidgetHelper::button(context, "##next", ImRect(ImVec2(origin.x + width - header, origin.y), ImVec2(origin.x + width, origin.y + header)), {}, Icon::Forward, ButtonVariant::Icon)) {
        m_shownMonth = m_shownMonth == 12 ? 1 : m_shownMonth + 1;
        m_shownYear = m_shownMonth == 1 ? m_shownYear + 1 : m_shownYear;
    }

    const std::vector<std::string> title{context.translate("workpane.calendar.month-" + std::to_string(m_shownMonth)), std::to_string(m_shownYear)};
    FontRole titleFont = font;
    titleFont.face = FontFace::SemiBold;
    WidgetHelper::alignedText(context, list, titleFont, ImRect(origin.x + header, origin.y, origin.x + width - header, origin.y + header), context.color(ThemeColor::Text), context.localization().translate("workpane.calendar.title", title), TextAlign::Center);

    const float weekdayTop = origin.y + header + spacing;
    const float weekdayHeight = WidgetHelper::lineHeight(context, context.font(ThemeFont::Caption));

    for (int weekday = 0; weekday < daysInWeek; ++weekday) {
        const float x = origin.x + static_cast<float>(weekday) * (cell + spacing);
        WidgetHelper::alignedText(context, list, context.font(ThemeFont::Caption), ImRect(x, weekdayTop, x + cell, weekdayTop + weekdayHeight), context.color(ThemeColor::TextMuted), context.translate("workpane.calendar.weekday-" + std::to_string(weekday)), TextAlign::Center);
    }

    // Six weeks always cover a month, and the days of the neighbouring months fill the grid in the muted ink.
    const float gridTop = weekdayTop + weekdayHeight + spacing;
    const int leading = firstWeekday(m_shownYear, m_shownMonth);
    const int previousMonth = m_shownMonth == 1 ? 12 : m_shownMonth - 1;
    const int previousYear = m_shownMonth == 1 ? m_shownYear - 1 : m_shownYear;
    const int previousDays = daysIn(previousYear, previousMonth);
    const int days = daysIn(m_shownYear, m_shownMonth);

    for (int index = 0; index < calendarWeeks * daysInWeek; ++index) {
        const int offset = index - leading;
        time::LocalMoment moment = m_value.value_or(time::LocalMoment{});
        moment.year = m_shownYear;
        moment.month = m_shownMonth;
        moment.day = offset + 1;

        if (offset < 0) {
            moment.year = previousYear;
            moment.month = previousMonth;
            moment.day = previousDays + offset + 1;
        } else if (offset >= days) {
            moment.year = m_shownMonth == 12 ? m_shownYear + 1 : m_shownYear;
            moment.month = m_shownMonth == 12 ? 1 : m_shownMonth + 1;
            moment.day = offset - days + 1;
        }

        const bool outside = offset < 0 || offset >= days;
        const bool chosen = m_value.has_value() && m_value->year == moment.year && m_value->month == moment.month && m_value->day == moment.day;
        const float x = origin.x + static_cast<float>(index % daysInWeek) * (cell + spacing);
        const float y = gridTop + static_cast<float>(index / daysInWeek) * (cell + spacing);
        const ImRect area(x, y, x + cell, y + cell);
        const ButtonState state = WidgetHelper::interact(ImGui::GetID(index), area);

        if (chosen || state.hovered) {
            list.AddRectFilled(area.Min, area.Max, WidgetHelper::ink(context.color(chosen ? ThemeColor::Accent : ThemeColor::Hover)), context.metric(ThemeMetric::ControlRadius));
        }

        WidgetHelper::alignedText(context, list, font, area, context.color(chosen ? ThemeColor::OnAccent : outside ? ThemeColor::TextMuted : ThemeColor::Text), std::to_string(moment.day), TextAlign::Center);

        if (state.pressed) {
            choose(context, moment);

            if (m_mode == MomentMode::Date) {
                ImGui::CloseCurrentPopup();
            }
        }
    }

    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(width, gridTop - origin.y + (cell + spacing) * calendarWeeks));
}

void DateTimeField::drawTime(RenderContext& context) {
    const float scale = context.scale();
    const float button = context.metric(ThemeMetric::CompactButtonSize);
    const float value = 28.0F * scale;
    const ImVec2 origin(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + calendarSpacing * 4.0F * scale);
    const float width = calendarCellSize * scale * daysInWeek + calendarSpacing * scale * (daysInWeek - 1);
    const float segment = button * 2.0F + value;
    const float separator = 12.0F * scale;
    float x = origin.x + (width - segment * 2.0F - separator) / 2.0F;
    time::LocalMoment moment = m_value.value_or(time::LocalMoment{m_shownYear, m_shownMonth, 1, 0, 0, 0});
    const std::array<int*, 2> fields{&moment.hour, &moment.minute};
    const std::array<int, 2> limits{24, 60};
    bool changed = false;

    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);

    for (std::size_t index = 0; index < fields.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));

        if (WidgetHelper::button(context, "##less", ImRect(x, origin.y, x + button, origin.y + button), {}, Icon::Minus, ButtonVariant::Icon)) {
            *fields[index] = (*fields[index] + limits[index] - 1) % limits[index];
            changed = true;
        }

        WidgetHelper::alignedText(context, *ImGui::GetWindowDrawList(), context.font(ThemeFont::Monospace), ImRect(x + button, origin.y, x + button + value, origin.y + button), context.color(ThemeColor::Text), twoDigits(*fields[index]), TextAlign::Center);

        if (WidgetHelper::button(context, "##more", ImRect(x + button + value, origin.y, x + segment, origin.y + button), {}, Icon::Add, ButtonVariant::Icon)) {
            *fields[index] = (*fields[index] + 1) % limits[index];
            changed = true;
        }

        ImGui::PopID();
        x += segment;

        if (index == 0) {
            WidgetHelper::alignedText(context, *ImGui::GetWindowDrawList(), context.font(ThemeFont::Monospace), ImRect(x, origin.y, x + separator, origin.y + button), context.color(ThemeColor::TextMuted), ":", TextAlign::Center);
            x += separator;
        }
    }

    ImGui::PopItemFlag();

    if (changed) {
        choose(context, moment);
    }

    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(width, button));
}

void DateTimeField::choose(RenderContext& context, const time::LocalMoment& moment) {
    m_value = moment;
    m_shownYear = moment.year;
    m_shownMonth = moment.month;
    context.emit(id(), "change", {{"value", format(moment, m_mode)}}, {{"value", "value"}});
}

} // namespace workpane::ui
