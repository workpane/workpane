-- The clock of the adventure: day from seven in the morning to seven in the evening and night the rest of the time, as the backgrounds of Flappy Bird follow it.
local daylight = {}

local dawn = 7
local dusk = 19

-- Answers the night of an hour of the clock with the second it has reached, so its stars twinkle in time, or nothing by day.
function daylight.night(hours)
    local hour = hours % 24

    if hour >= dawn and hour < dusk then
        return nil
    end

    return { seconds = hour * 3600 }
end

-- Answers the hour of the computer clock with its minutes and seconds.
function daylight.now()
    local moment = os.date("*t")

    return moment.hour + moment.min / 60 + moment.sec / 3600
end

return daylight
