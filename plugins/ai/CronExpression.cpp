#include "CronExpression.h"

#include <QStringList>

#include <optional>
#include <utility>

namespace workpane::plugins::ai {

constexpr int hoursInDay = 24;
constexpr int minutesInHour = 60;
constexpr int secondsInMinute = 60;
constexpr int secondsInHour = 3600;
constexpr int supportedSchedulingYears = 8;

struct ParsedField final {
    QBitArray values;
    bool wildcard{false};
};

class CronExpressionHelper final {
  public:
    static Result<int> parseNumber(const QString& value, int minimum, int maximum);
    static Result<ParsedField> parseField(const QString& field, int minimum, int maximum, bool normalizeSunday);
    static std::optional<QDateTime> instantOf(const QDate& date, const QTime& time, const QTimeZone& timeZone);
};

Result<int> CronExpressionHelper::parseNumber(const QString& value, int minimum, int maximum) {
    bool valid = false;
    const int number = value.toInt(&valid);

    if (!valid || number < minimum || number > maximum) {
        return Result<int>::failure({"ai_tasks_cron_value_invalid", "The cron expression contains an invalid value", value});
    }

    return Result<int>::success(number);
}

Result<ParsedField> CronExpressionHelper::parseField(const QString& field, int minimum, int maximum, bool normalizeSunday) {
    ParsedField parsed{QBitArray(maximum + 1), field == QStringLiteral("*")};

    if (parsed.wildcard) {
        parsed.values.fill(true, minimum, maximum + 1);
        return Result<ParsedField>::success(std::move(parsed));
    }

    if (field.isEmpty()) {
        return Result<ParsedField>::failure({"ai_tasks_cron_field_invalid", "The cron expression contains an empty field", {}});
    }

    for (const auto& element : field.split(QLatin1Char(','))) {
        const QStringList range = element.split(QLatin1Char('-'));
        if (range.size() < 1 || range.size() > 2 || range.first().isEmpty() || range.last().isEmpty()) {
            return Result<ParsedField>::failure({"ai_tasks_cron_field_invalid", "The cron expression contains an invalid field", field});
        }
        const auto first = parseNumber(range.first(), minimum, maximum);
        const auto last = parseNumber(range.last(), minimum, maximum);
        if (!first.hasValue() || !last.hasValue() || first.value() > last.value()) {
            return Result<ParsedField>::failure(!first.hasValue() ? first.error() : !last.hasValue() ? last.error() : Error{"ai_tasks_cron_range_invalid", "The cron expression contains an invalid range", element});
        }
        for (int value = first.value(); value <= last.value(); ++value) {
            parsed.values.setBit(normalizeSunday && value == 7 ? 0 : value);
        }
    }

    return Result<ParsedField>::success(std::move(parsed));
}

CronExpression::CronExpression(QBitArray minutes, QBitArray hours, QBitArray monthDays, QBitArray months, QBitArray weekDays, bool monthDayWildcard, bool weekDayWildcard) : m_minutes(std::move(minutes)), m_hours(std::move(hours)), m_monthDays(std::move(monthDays)), m_months(std::move(months)), m_weekDays(std::move(weekDays)), m_monthDayWildcard(monthDayWildcard), m_weekDayWildcard(weekDayWildcard) {}

Result<CronExpression> CronExpression::parse(const QString& expression) {
    const QStringList fields = expression.simplified().split(QLatin1Char(' '));

    if (fields.size() != 5) {
        return Result<CronExpression>::failure({"ai_tasks_cron_field_count_invalid", "A POSIX cron expression requires five fields", expression});
    }

    const auto minutes = CronExpressionHelper::parseField(fields.at(0), 0, 59, false);
    const auto hours = CronExpressionHelper::parseField(fields.at(1), 0, 23, false);
    const auto monthDays = CronExpressionHelper::parseField(fields.at(2), 1, 31, false);
    const auto months = CronExpressionHelper::parseField(fields.at(3), 1, 12, false);
    const auto weekDays = CronExpressionHelper::parseField(fields.at(4), 0, 7, true);

    if (!minutes.hasValue() || !hours.hasValue() || !monthDays.hasValue() || !months.hasValue() || !weekDays.hasValue()) {
        const Error error = !minutes.hasValue() ? minutes.error() : !hours.hasValue() ? hours.error() : !monthDays.hasValue() ? monthDays.error() : !months.hasValue() ? months.error() : weekDays.error();
        return Result<CronExpression>::failure(error);
    }

    return Result<CronExpression>::success(CronExpression(minutes.value().values, hours.value().values, monthDays.value().values, months.value().values, weekDays.value().values, monthDays.value().wildcard, weekDays.value().wildcard));
}

// A wall clock the calendar skips is answered by the first one it does carry, and a wall clock it repeats is answered by the first of the two.
std::optional<QDateTime> CronExpressionHelper::instantOf(const QDate& date, const QTime& time, const QTimeZone& timeZone) {
    const QDateTime local(date, time, timeZone);

    if (!local.isValid()) {
        return std::nullopt;
    }

    if (local.date() == date && local.time() == time) {
        const QDateTime earlier = local.addSecs(-secondsInHour);
        const bool repeated = earlier.isValid() && earlier.date() == date && earlier.time() == time;
        return repeated ? earlier.toUTC() : local.toUTC();
    }

    // The clock moved forward over that wall time, so the occurrence is the instant the day really starts carrying it.
    const QDateTime midnight(date, QTime(0, 0), timeZone);

    for (int minute = 0; minute < minutesInHour * hoursInDay; ++minute) {
        const QDateTime probe = midnight.addSecs(secondsInMinute * minute);
        if (probe.date() > date || (probe.date() == date && probe.time() >= time)) {
            return probe.toUTC();
        }
    }

    return std::nullopt;
}

// The occurrence is chosen on the wall clock of the schedule and only then turned into an instant, because a wall clock is what the expression is written in.
Result<QDateTime> CronExpression::nextAfter(const QDateTime& afterUtc, const QTimeZone& timeZone) const {
    if (!afterUtc.isValid() || afterUtc.timeSpec() != Qt::UTC || !timeZone.isValid()) {
        return Result<QDateTime>::failure({"ai_tasks_schedule_context_invalid", "The cron scheduling context is invalid", {}});
    }

    const QDateTime local = afterUtc.toTimeZone(timeZone);
    const QDate startDate = local.date();
    const QTime startTime = local.time();

    for (QDate date = startDate; date < startDate.addYears(supportedSchedulingYears); date = date.addDays(1)) {
        if (!matchesDate(date)) {
            continue;
        }

        for (int hour = 0; hour < hoursInDay; ++hour) {
            if (!m_hours.testBit(hour)) {
                continue;
            }
            for (int minute = 0; minute < minutesInHour; ++minute) {
                if (!m_minutes.testBit(minute)) {
                    continue;
                }
                if (date == startDate && QTime(hour, minute) <= QTime(startTime.hour(), startTime.minute())) {
                    continue;
                }

                const auto instant = CronExpressionHelper::instantOf(date, QTime(hour, minute), timeZone);
                if (instant.has_value() && instant.value() > afterUtc) {
                    return Result<QDateTime>::success(instant.value());
                }
            }
        }
    }

    return Result<QDateTime>::failure({"ai_tasks_cron_occurrence_unavailable", "The cron expression has no occurrence within the supported scheduling horizon", {}});
}

bool CronExpression::matchesDate(const QDate& date) const {
    const bool monthMatches = m_months.testBit(date.month());
    const bool monthDayMatches = m_monthDays.testBit(date.day());
    const int weekDay = date.dayOfWeek() == 7 ? 0 : date.dayOfWeek();
    const bool weekDayMatches = m_weekDays.testBit(weekDay);

    if (m_weekDayWildcard) {
        return monthMatches && monthDayMatches;
    }
    if (m_monthDayWildcard) {
        return monthMatches && weekDayMatches;
    }

    return monthMatches && (monthDayMatches || weekDayMatches);
}

} // namespace workpane::plugins::ai
