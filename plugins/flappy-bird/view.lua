-- The destination of the game: a canvas that fills the view and runs a round sixty times a second while it is on screen.
local drawing = include("drawing")
local game = include("game")
local preferences = include("preferences")
local store = include("store")

local ui = workpane.ui

local view = {}

local frameRate = 60
local colors = { "yellow", "blue", "red" }
local flapKeys = { space = true, up = true, w = true, enter = true }
local pauseKeys = { escape = true, p = true }

local function play(sounds)
    if not preferences.get("sound") then
        return
    end

    for _, sound in ipairs(sounds) do
        workpane.audio.play("audio/" .. sound .. ".wav", { volume = 0.6 })
    end
end

-- The bird of a random color and the background that follows the clock are chosen again for every round.
local function look()
    local bird = preferences.get("bird")
    local background = preferences.get("background")
    local hour = tonumber(os.date("%H"))

    return {
        bird = bird == "random" and colors[math.random(#colors)] or bird,
        night = background == "night" or (background == "clock" and (hour < 7 or hour >= 19)),
    }
end

-- The best rounds are read with their moments written in the local time of the reader.
local function rounds()
    local presented = {}

    for index, round in ipairs(store.best()) do
        presented[index] = { score = round.score, date = workpane.time.localPresentation(round.playedAt) }
    end

    return presented
end

-- The canvas ticks while a round waits, runs or falls, and stops once the board of a round that ended can start the next one, so a board or a pause costs nothing while it waits.
function view.build()
    local state = game.new(game.widestField, math.random)
    local chosen = look()
    local best = {}
    local size = { width = 1, height = game.worldHeight }
    local ticking = true
    local canvas

    local function draw()
        canvas:command("draw", { commands = drawing.commands(state, size, { bird = chosen.bird, night = chosen.night, best = preferences.get("best"), rounds = best }) })
    end

    local function tick(on)
        if ticking ~= on then
            ticking = on
            canvas:set({ frameRate = on and frameRate or 0 })
        end
    end

    -- A round that ends is kept among the best ones, which the board shows once they are read again.
    local function finish()
        if state.score > preferences.get("best") then
            workpane.await(preferences.set("best", state.score))
        end

        store.record(state.score)
    end

    local function act(action)
        local before = state.mode
        play(action(state) or {})

        if before == "over" and state.mode == "ready" then
            chosen = look()
            tick(true)
        end
    end

    local function pause()
        act(game.pause)
        tick(not state.paused)
        draw()
    end

    canvas = ui.canvas({ grow = 1, frameRate = frameRate, pixelated = true, focusable = true, pictures = drawing.pictures(colors), onFrame = function(frame)
        size = { width = frame.width, height = frame.height }
        game.resize(state, frame.width * game.worldHeight / math.max(1, frame.height))
        local before = state.mode
        play(game.step(state, frame.delta))

        if before ~= "over" and state.mode == "over" then
            workpane.task(finish)
        end

        draw()
        tick(not game.restartable(state))
    end, onLayout = function(event)
        size = { width = event.width, height = event.height }
        game.resize(state, event.width * game.worldHeight / math.max(1, event.height))

        if not ticking then
            draw()
        end
    end, onPointerDown = function()
        act(game.flap)
    end, onKeyDown = function(event)
        if flapKeys[event.key] then
            act(game.flap)
        elseif pauseKeys[event.key] then
            pause()
        end
    end })

    -- The board follows the best rounds as they are recorded or forgotten, drawn again when the canvas no longer ticks.
    local function reload()
        best = rounds()

        if not ticking then
            draw()
        end
    end

    store.watch(reload)
    workpane.task(reload)
    canvas:command("focus")

    return ui.column({ grow = 1 }, { canvas })
end

return view
