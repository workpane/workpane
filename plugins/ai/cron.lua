-- POSIX cron expressions of five numeric fields, answered on the wall clock of a named time zone, and the stored UTC instants schedules are kept in.
local cron = {}

local horizonYears = 8
local secondsInDay = 86400

local fields = {
    { name = "minute", minimum = 0, maximum = 59 },
    { name = "hour", minimum = 0, maximum = 23 },
    { name = "day", minimum = 1, maximum = 31 },
    { name = "month", minimum = 1, maximum = 12 },
    { name = "weekday", minimum = 0, maximum = 7 },
}

local function refuse(code, message, detail)
    error({ code = code, message = message, detail = detail or "" }, 0)
end

local function number(text, field)
    local value = text:match("^%d+$") and tonumber(text) or nil

    if value == nil or value < field.minimum or value > field.maximum then
        refuse("ai_tasks_cron_value_invalid", "The cron expression contains an invalid value", text)
    end

    return value
end

-- A field is a star, a value, a range or a comma list of them, and seven is Sunday like zero.
local function parseField(text, field)
    local values = {}

    if text == "*" then
        for value = field.minimum, field.maximum do
            values[field.name == "weekday" and value % 7 or value] = true
        end

        return values, true
    end

    for element in (text .. ","):gmatch("([^,]*),") do
        local first, last = element:match("^([^-]+)-([^-]+)$")
        first = first or element
        last = last or element

        if element == "" then
            refuse("ai_tasks_cron_field_invalid", "The cron expression contains an empty field", text)
        end

        local low = number(first, field)
        local high = number(last, field)

        if low > high then
            refuse("ai_tasks_cron_range_invalid", "The cron expression contains an invalid range", element)
        end

        for value = low, high do
            values[field.name == "weekday" and value % 7 or value] = true
        end
    end

    return values, false
end

function cron.parse(expression)
    local parts = {}

    for part in expression:gmatch("%S+") do
        parts[#parts + 1] = part
    end

    if #parts ~= 5 then
        refuse("ai_tasks_cron_field_count_invalid", "A POSIX cron expression requires five fields", expression)
    end

    local parsed = {}

    for index, field in ipairs(fields) do
        local values, wildcard = parseField(parts[index], field)
        parsed[field.name] = values
        parsed[field.name .. "Wildcard"] = wildcard
    end

    return parsed
end

-- The day of the month and the day of the week join with the POSIX rule: either one matches when neither is a star.
local function matchesDate(parsed, date)
    local month = parsed.month[date.month] == true
    local day = parsed.day[date.day] == true
    local weekday = parsed.weekday[date.wday - 1] == true

    if parsed.weekdayWildcard then
        return month and day
    end

    if parsed.dayWildcard then
        return month and weekday
    end

    return month and (day or weekday)
end

-- Days from the civil calendar turn a date into seconds without asking any zone.
local function daysFromCivil(year, month, day)
    local shifted = month <= 2 and year - 1 or year
    local era = (shifted >= 0 and shifted or shifted - 399) // 400
    local yearOfEra = shifted - era * 400
    local dayOfYear = (153 * (month + (month > 2 and -3 or 9)) + 2) // 5 + day - 1
    local dayOfEra = yearOfEra * 365 + yearOfEra // 4 - yearOfEra // 100 + dayOfYear
    return era * 146097 + dayOfEra - 719468
end

-- The wall clock of an instant in a zone, as seconds counted like a UTC instant, comes from the offset the zone has at that instant.
local function wallOf(zone, moment)
    return moment + workpane.time.offset(zone, moment)
end

-- A wall clock the zone skips is answered by the first one it does carry, and a wall clock it repeats by the first of the two.
local function instantOf(zone, date, hour, minute)
    local wall = daysFromCivil(date.year, date.month, date.day) * secondsInDay + hour * 3600 + minute * 60
    local before = wall - workpane.time.offset(zone, wall - secondsInDay)
    local after = wall - workpane.time.offset(zone, wall + secondsInDay)
    local first, last = math.min(before, after), math.max(before, after)

    if wallOf(zone, first) == wall then
        return first
    end

    if wallOf(zone, last) == wall then
        return last
    end

    for probe = first, last, 60 do
        if wallOf(zone, probe) >= wall then
            return probe
        end
    end

    return nil
end

-- The occurrence is chosen on the wall clock of the zone and only then turned into an instant, because a wall clock is what the expression is written in.
function cron.nextAfter(parsed, after, zone)
    local start = os.date("!*t", wallOf(zone, after))
    local firstDay = daysFromCivil(start.year, start.month, start.day)

    for offset = 0, horizonYears * 366 do
        local date = os.date("!*t", (firstDay + offset) * secondsInDay)

        if matchesDate(parsed, date) then
            for hour = 0, 23 do
                for minute = 0, 59 do
                    local sameDay = offset == 0 and hour * 60 + minute <= start.hour * 60 + start.min

                    if parsed.hour[hour] and parsed.minute[minute] and not sameDay then
                        local moment = instantOf(zone, date, hour, minute)

                        if moment ~= nil and moment > after then
                            return moment
                        end
                    end
                end
            end
        end
    end

    refuse("ai_tasks_cron_occurrence_unavailable", "The cron expression has no occurrence within the supported scheduling horizon", "")
end

function cron.epoch(stamp)
    local year, month, day, hour, minute, second = stamp:match("^(%d+)%-(%d+)%-(%d+)T(%d+):(%d+):(%d+)")

    if year == nil then
        return nil
    end

    return daysFromCivil(tonumber(year), tonumber(month), tonumber(day)) * secondsInDay + tonumber(hour) * 3600 + tonumber(minute) * 60 + tonumber(second)
end

function cron.stamp(moment)
    return os.date("!%Y-%m-%dT%H:%M:%S", moment) .. ".000Z"
end

-- A wall clock of a zone written as the date and time fields of the date picker becomes the instant it names.
function cron.localMoment(text, zone)
    local year, month, day, hour, minute = text:match("^(%d%d%d%d)%-(%d%d)%-(%d%d) (%d%d):(%d%d)$")

    if year == nil then
        return nil
    end

    return instantOf(zone, { year = tonumber(year), month = tonumber(month), day = tonumber(day) }, tonumber(hour), tonumber(minute))
end

function cron.localText(moment, zone)
    return os.date("!%Y-%m-%d %H:%M", wallOf(zone, moment))
end

return cron
