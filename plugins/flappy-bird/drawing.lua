-- Draws a round on a canvas of any size, scaling the world of the original game to the height of the canvas and repeating its background and its ground across the width.
local game = include("game")

local text = workpane.i18n.text

local drawing = {}

local backgroundWidth = 288
local groundWidth = 336
local groundHeight = 112
local digitHeight = 36
local messageWidth = 184
local messageHeight = 267
local overWidth = 192
local overHeight = 42
local smallestHeight = 8

local function digitWidth(digit)
    return digit == "1" and 16 or 24
end

-- Answers every picture a round draws for birds of the given colors, which the canvas keeps ready from the start, so no picture is missing the first time it shows.
function drawing.pictures(colors)
    local listed = { "sprites/background-day.png", "sprites/background-night.png", "sprites/base.png", "sprites/gameover.png", "sprites/message.png", "sprites/pipe-green.png", "sprites/pipe-red.png" }

    for digit = 0, 9 do
        listed[#listed + 1] = "sprites/" .. digit .. ".png"
    end

    for _, color in ipairs(colors) do
        for _, wing in ipairs(game.wings) do
            listed[#listed + 1] = "sprites/" .. color .. "bird-" .. wing .. ".png"
        end
    end

    return listed
end

-- A picture repeats in one command from a point left of the canvas, so every tile lines up with the one at the given position.
local function tiles(commands, image, position, width, y, height, canvasWidth)
    local start = position - math.ceil(position / width) * width
    commands[#commands + 1] = { op = "image", image = image, x = start, y = y, width = canvasWidth - start, height = height, tile = { width = width, height = height } }
end

-- The score is written in the digit sprites of the original, centered over the field.
local function score(commands, value, centre, top, scale)
    local digits = tostring(value)
    local width = 0

    for digit in digits:gmatch("%d") do
        width = width + digitWidth(digit) * scale
    end

    local x = centre - width / 2

    for digit in digits:gmatch("%d") do
        commands[#commands + 1] = { op = "image", image = "sprites/" .. digit .. ".png", x = x, y = top, width = digitWidth(digit) * scale, height = digitHeight * scale }
        x = x + digitWidth(digit) * scale
    end
end

-- The board after a round names its score, the best score and the best rounds with their dates.
local function board(commands, look, value, left, fieldWidth, scale)
    local top = game.worldHeight * 0.28 * scale
    local line = 18 * scale
    local size = math.max(7, math.min(40, 13 * scale))
    local centre = left + fieldWidth / 2
    local height = (4 + #look.rounds) * line

    commands[#commands + 1] = { op = "rect", x = left + 16 * scale, y = top, width = fieldWidth - 32 * scale, height = height, color = "#000000", opacity = 0.45, radius = 8 * scale }
    commands[#commands + 1] = { op = "text", x = centre, y = top + line * 0.5, text = text("flappy-bird.game.score", value), color = "#ffffff", size = size * 1.2, face = "semibold", align = "center" }
    commands[#commands + 1] = { op = "text", x = centre, y = top + line * 1.7, text = text("flappy-bird.game.best", look.best), color = "#ffe38a", size = size, align = "center" }
    commands[#commands + 1] = { op = "text", x = centre, y = top + line * 2.8, text = text("flappy-bird.game.rounds"), color = "#ffffff", size = size, face = "semibold", align = "center" }

    for index, round in ipairs(look.rounds) do
        commands[#commands + 1] = { op = "text", x = centre, y = top + line * (2.8 + index), text = text("flappy-bird.game.round", index, round.score, round.date), color = "#ffffff", size = size * 0.9, face = "monospace", align = "center" }
    end

    commands[#commands + 1] = { op = "text", x = centre, y = top + height + line * 0.5, text = text("flappy-bird.game.again"), color = "#ffffff", size = size * 0.9, align = "center" }
end

-- Answers the commands that draw the round on a canvas of the given size, in the colors and the time of day the look names.
-- A canvas too short to show the game draws nothing, since its pictures would repeat below the size of a point.
function drawing.commands(state, canvas, look)
    if canvas.height < smallestHeight then
        return {}
    end

    local scale = canvas.height / game.worldHeight
    local fieldWidth = state.width * scale
    local left = math.floor((canvas.width - fieldWidth) / 2)
    local groundTop = game.groundY * scale
    local commands = {}

    tiles(commands, look.night and "sprites/background-night.png" or "sprites/background-day.png", left, backgroundWidth * scale, 0, game.worldHeight * scale, canvas.width)
    commands[#commands + 1] = { op = "clip", x = left, y = 0, width = fieldWidth, height = groundTop }

    for _, pipe in ipairs(state.pipes) do
        local image = look.night and "sprites/pipe-red.png" or "sprites/pipe-green.png"
        local x = left + pipe.x * scale
        commands[#commands + 1] = { op = "image", image = image, x = x, y = (pipe.gapY - game.pipeHeight) * scale, width = game.pipeWidth * scale, height = game.pipeHeight * scale, flipY = true }
        commands[#commands + 1] = { op = "image", image = image, x = x, y = (pipe.gapY + game.gap) * scale, width = game.pipeWidth * scale, height = game.pipeHeight * scale }
    end

    commands[#commands + 1] = { op = "unclip" }
    tiles(commands, "sprites/base.png", left - state.groundShift * scale, groundWidth * scale, groundTop, groundHeight * scale, canvas.width)

    -- The sides beyond the field are shaded, so a pipe reads as coming in from the edge of the field.
    if left > 0 then
        commands[#commands + 1] = { op = "rect", x = 0, y = 0, width = left, height = canvas.height, color = "#000000", opacity = 0.3 }
        commands[#commands + 1] = { op = "rect", x = left + fieldWidth, y = 0, width = canvas.width - left - fieldWidth, height = canvas.height, color = "#000000", opacity = 0.3 }
    end

    commands[#commands + 1] = { op = "image", image = "sprites/" .. look.bird .. "bird-" .. game.wing(state) .. ".png", x = left + game.birdX * scale, y = state.birdY * scale, width = game.birdWidth * scale, height = game.birdHeight * scale, rotation = -math.rad(game.visibleTurn(state)) }

    if state.mode == "ready" then
        commands[#commands + 1] = { op = "image", image = "sprites/message.png", x = left + (fieldWidth - messageWidth * scale) / 2, y = game.worldHeight * 0.12 * scale, width = messageWidth * scale, height = messageHeight * scale }
    else
        score(commands, state.score, left + fieldWidth / 2, game.worldHeight * 0.08 * scale, scale)
    end

    if state.mode == "over" then
        commands[#commands + 1] = { op = "image", image = "sprites/gameover.png", x = left + (fieldWidth - overWidth * scale) / 2, y = game.worldHeight * 0.18 * scale, width = overWidth * scale, height = overHeight * scale }
        board(commands, look, state.score, left, fieldWidth, scale)
    end

    if state.paused then
        commands[#commands + 1] = { op = "rect", x = left, y = 0, width = fieldWidth, height = groundTop, color = "#000000", opacity = 0.35 }
        commands[#commands + 1] = { op = "text", x = left + fieldWidth / 2, y = groundTop / 2, text = text("flappy-bird.game.paused"), color = "#ffffff", size = math.max(8, math.min(64, 24 * scale)), face = "semibold", align = "center" }
    end

    return commands
end

return drawing
