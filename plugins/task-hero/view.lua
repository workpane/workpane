-- The band of the adventure: a canvas across the bottom of the window where the knight walks thirty frames a second, paused and resumed by a click.
local adventure = include("adventure")
local daylight = include("daylight")
local drawing = include("drawing")
local preferences = include("preferences")
local progress = include("progress")
local sheets = include("sheets")

local ui = workpane.ui

local view = {}

local frameRate = 30
local latest

-- The progress the band has not written yet is written before the plugin stops.
function view.stop()
    local written = latest ~= nil and progress.flush(latest) or nil

    if written ~= nil then
        written:await()
    end
end

function view.build()
    local state = adventure.new(preferences.get("progress"), math.random)
    local size = { width = 1, height = adventure.height }
    local canvas
    local keeper = progress.keeper(preferences.keep)
    latest = keeper

    -- The night comes with the clock only once the reader asks for it, and otherwise the adventure stays in the day it always had.
    local function draw()
        local night = preferences.get("dayNight") and daylight.night(daylight.now()) or nil
        canvas:command("draw", { commands = drawing.commands(state, size, preferences.get("counters"), night) })
    end

    canvas = ui.canvas({ grow = 1, frameRate = frameRate, pixelated = true, pictures = sheets.pictures(), onFrame = function(frame)
        size = { width = frame.width, height = frame.height }
        adventure.resize(state, frame.width * adventure.height / math.max(1, frame.height))

        -- A victory, a level, a fall, gold that lands and a new kind of warrior change the progress, which is kept so the adventure goes on after a restart.
        if #adventure.step(state, frame.delta, preferences.speed()) > 0 then
            progress.changed(keeper, state.progress)
        end

        progress.tick(keeper, frame.delta)
        draw()
    end, onPointerDown = function()
        -- A paused adventure stops ticking altogether and shows its pause once, so it costs nothing while it waits, and its progress is written at once.
        adventure.pause(state)
        canvas:set({ frameRate = state.paused and 0 or frameRate })
        draw()

        if state.paused then
            progress.flush(keeper)
        end
    end })

    -- A paused adventure takes the day or the night at once, since it draws nothing more until it resumes.
    preferences.watch("dayNight", function()
        draw()
    end)

    -- A progress set back from the settings starts the knight over on a band that stays as wide and as paused as it was, while the progress this band keeps itself changes nothing here.
    preferences.watch("progress", function(value)
        if value.victories >= state.progress.victories and value.level >= state.progress.level then
            return
        end

        local previous = state
        progress.forget(keeper)
        state = adventure.new(value, math.random)
        state.paused = previous.paused
        adventure.resize(state, previous.width)
        draw()
    end)

    return canvas
end

return view
