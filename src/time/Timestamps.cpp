#include "time/Timestamps.h"

#include <array>
#include <charconv>
#include <cstdio>
#include <ctime>

namespace workpane::time {

bool Timestamps::readNumber(std::string_view text, std::size_t offset, std::size_t length, int& out) {
    const std::string_view digits = text.substr(offset, length);

    for (const char digit : digits) {
        if (digit < '0' || digit > '9') {
            return false;
        }
    }

    const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), out);
    return result.ec == std::errc{} && result.ptr == digits.data() + digits.size();
}

// The reentrant form is used because the plain one answers into storage the whole process shares.
bool Timestamps::localCalendar(std::time_t seconds, std::tm& out) {
#if defined(_WIN32)
    return localtime_s(&out, &seconds) == 0;
#else
    return localtime_r(&seconds, &out) != nullptr;
#endif
}

Instant Timestamps::now() {
    return std::chrono::floor<std::chrono::milliseconds>(Clock::now());
}

std::string Timestamps::storedTimestamp(Instant instant) {
    const auto days = std::chrono::floor<std::chrono::days>(instant);
    const std::chrono::year_month_day date(days);
    const std::chrono::hh_mm_ss time(instant - days);
    std::array<char, 32> buffer{};
    const int written = std::snprintf(buffer.data(), buffer.size(), "%04d-%02u-%02uT%02d:%02d:%02d.%03dZ", static_cast<int>(date.year()), static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()), static_cast<int>(time.hours().count()), static_cast<int>(time.minutes().count()), static_cast<int>(time.seconds().count()), static_cast<int>(time.subseconds().count()));
    return {buffer.data(), static_cast<std::size_t>(written)};
}

std::optional<Instant> Timestamps::parseStoredTimestamp(std::string_view text) {
    constexpr std::size_t storedLength = 24;

    if (text.size() != storedLength || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' || text[16] != ':' || text[19] != '.' || text[23] != 'Z') {
        return std::nullopt;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int millisecond = 0;

    if (!readNumber(text, 0, 4, year) || !readNumber(text, 5, 2, month) || !readNumber(text, 8, 2, day) || !readNumber(text, 11, 2, hour) || !readNumber(text, 14, 2, minute) || !readNumber(text, 17, 2, second) || !readNumber(text, 20, 3, millisecond)) {
        return std::nullopt;
    }

    const std::chrono::year_month_day date{std::chrono::year(year), std::chrono::month(static_cast<unsigned>(month)), std::chrono::day(static_cast<unsigned>(day))};

    if (!date.ok() || hour > 23 || minute > 59 || second > 59) {
        return std::nullopt;
    }

    return Instant(std::chrono::sys_days(date).time_since_epoch() + std::chrono::hours(hour) + std::chrono::minutes(minute) + std::chrono::seconds(second) + std::chrono::milliseconds(millisecond));
}

// A moment the platform cannot place in the local zone has no presentation, which the caller reports rather than guessing one.
std::optional<std::string> Timestamps::localPresentation(Instant instant) {
    const auto moment = localMoment(instant);

    if (!moment.has_value()) {
        return std::nullopt;
    }

    std::array<char, 32> buffer{};
    const int written = std::snprintf(buffer.data(), buffer.size(), "%04d-%02d-%02d %02d:%02d:%02d", moment->year, moment->month, moment->day, moment->hour, moment->minute, moment->second);
    return std::string(buffer.data(), static_cast<std::size_t>(written));
}

// The instant is counted in whole seconds, since the clock of the system counts nanoseconds on Linux and overflows for dates far from the present.
std::optional<LocalMoment> Timestamps::localMoment(Instant instant) {
    std::tm calendar{};
    const auto seconds = static_cast<std::time_t>(std::chrono::floor<std::chrono::seconds>(instant).time_since_epoch().count());

    if (!localCalendar(seconds, calendar)) {
        return std::nullopt;
    }

    return LocalMoment{calendar.tm_year + 1900, calendar.tm_mon + 1, calendar.tm_mday, calendar.tm_hour, calendar.tm_min, calendar.tm_sec};
}

} // namespace workpane::time
