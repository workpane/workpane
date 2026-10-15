-- The rules of Flappy Bird in the units of the original game, a world 512 units tall, advanced by the seconds of each tick and answering the sounds of what happened.
local game = {}

game.worldHeight = 512
game.groundY = 404
game.birdX = 57
game.birdWidth = 34
game.birdHeight = 24
game.pipeWidth = 52
game.pipeHeight = 320
game.gap = 100
game.widestField = 432

local gravity = 900
local fastestFall = 300
local flapSpeed = -270
local pipeSpeed = 120
local pipeSpacing = 144
local firstPipe = 100
local flapTurn = 45
local turnSpeed = 90
local lowestTurn = -90
local wingSeconds = 0.1
local restartSeconds = 0.6
local hitInset = 3
local wings = { "upflap", "midflap", "downflap", "midflap" }

-- The poses of the wings in the order they beat, which the pictures of every bird are named after.
game.wings = wings

-- A new round waits for the first flap with the bird floating where the original game shows it.
function game.new(width, random)
    return {
        mode = "ready",
        width = math.min(width, game.widestField),
        random = random,
        birdY = 244,
        velocity = 0,
        turn = 0,
        wing = 1,
        wingTime = 0,
        pipes = {},
        score = 0,
        groundShift = 0,
        time = 0,
        endedAt = 0,
        paused = false,
    }
end

-- The field keeps the height of the original game and grows sideways with the canvas up to half as wide again as the original.
function game.resize(state, width)
    state.width = math.min(width, game.widestField)
end

function game.wing(state)
    return wings[state.wing]
end

-- A turn above twenty degrees is drawn as twenty, as the original draws a bird that just flapped.
function game.visibleTurn(state)
    return math.min(state.turn, 20)
end

local function spawn(state, x)
    local gapY = math.floor(state.random() * (game.groundY * 0.6 - game.gap)) + math.floor(game.groundY * 0.2)
    state.pipes[#state.pipes + 1] = { x = x, gapY = gapY, scored = false }
end

-- A flap starts a round that waits, lifts the bird of a round that runs and starts a new round once the last one ended a moment ago.
function game.flap(state)
    if state.paused then
        return {}
    end

    if state.mode == "ready" then
        state.mode = "playing"
        state.velocity = flapSpeed
        state.turn = flapTurn
        spawn(state, state.width + firstPipe)

        return { "swoosh", "wing" }
    end

    if state.mode == "playing" then
        if state.birdY > -2 * game.birdHeight then
            state.velocity = flapSpeed
            state.turn = flapTurn
        end

        return { "wing" }
    end

    if game.restartable(state) then
        local fresh = game.new(state.width, state.random)

        for key, value in pairs(fresh) do
            state[key] = value
        end

        return { "swoosh" }
    end

    return {}
end

-- A round that ended starts again on a flap only a moment after it ended, so the flap that lost it never skips the board.
function game.restartable(state)
    return state.mode == "over" and state.time - state.endedAt >= restartSeconds
end

function game.pause(state)
    if state.mode == "playing" then
        state.paused = not state.paused
    end
end

local function overlaps(state, pipe)
    local left = game.birdX + hitInset
    local right = game.birdX + game.birdWidth - hitInset
    local top = state.birdY + hitInset
    local bottom = state.birdY + game.birdHeight - hitInset
    local inside = right > pipe.x and left < pipe.x + game.pipeWidth

    return inside and (top < pipe.gapY or bottom > pipe.gapY + game.gap)
end

-- The bird falls under gravity, turns with its fall and flaps its wings, and the pipes move toward it.
local function move(state, seconds)
    state.velocity = math.min(state.velocity + gravity * seconds, fastestFall)
    state.birdY = math.min(state.birdY + state.velocity * seconds, game.groundY - game.birdHeight)
    state.turn = math.max(state.turn - turnSpeed * seconds, lowestTurn)
end

local function flutter(state, seconds)
    state.wingTime = state.wingTime + seconds

    while state.wingTime >= wingSeconds do
        state.wingTime = state.wingTime - wingSeconds
        state.wing = state.wing % #wings + 1
    end
end

local function scroll(state, seconds)
    local shift = pipeSpeed * seconds
    state.groundShift = (state.groundShift + shift) % 336

    for _, pipe in ipairs(state.pipes) do
        pipe.x = pipe.x - shift
    end

    while state.pipes[1] ~= nil and state.pipes[1].x < -game.pipeWidth do
        table.remove(state.pipes, 1)
    end

    local last = state.pipes[#state.pipes]

    if last ~= nil and last.x < state.width + 10 - pipeSpacing then
        spawn(state, last.x + pipeSpacing)
    end
end

local function playing(state, seconds, sounds)
    scroll(state, seconds)
    move(state, seconds)
    flutter(state, seconds)
    local centre = game.birdX + game.birdWidth / 2

    for _, pipe in ipairs(state.pipes) do
        if not pipe.scored and pipe.x + game.pipeWidth / 2 <= centre then
            pipe.scored = true
            state.score = state.score + 1
            sounds[#sounds + 1] = "point"
        end

        if overlaps(state, pipe) then
            state.mode = "falling"
            sounds[#sounds + 1] = "hit"
            sounds[#sounds + 1] = "die"

            return
        end
    end

    if state.birdY >= game.groundY - game.birdHeight then
        state.mode = "over"
        state.endedAt = state.time
        sounds[#sounds + 1] = "hit"
    end
end

-- Advances the round by the seconds of a tick and answers the sounds of what happened in it.
function game.step(state, seconds)
    local sounds = {}

    if state.paused then
        return sounds
    end

    state.time = state.time + seconds

    if state.mode == "ready" then
        flutter(state, seconds)
        state.groundShift = (state.groundShift + pipeSpeed * seconds) % 336
        state.birdY = 244 + math.sin(state.time * 8) * 4
    elseif state.mode == "playing" then
        playing(state, seconds, sounds)
    elseif state.mode == "falling" then
        move(state, seconds)

        if state.birdY >= game.groundY - game.birdHeight then
            state.mode = "over"
            state.endedAt = state.time
        end
    end

    return sounds
end

return game
