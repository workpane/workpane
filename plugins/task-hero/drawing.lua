-- Draws the adventure across a band of any size, scaling the world of 96 units to the height of the band and keeping the knight near its left edge.
local adventure = include("adventure")
local sheets = include("sheets")

local text = workpane.i18n.text

local drawing = {}

local figureScale = adventure.figureScale
local sceneryScale = adventure.sceneryScale
local cloudScale = adventure.cloudScale
local itemSize = adventure.itemSize
local tileSize = adventure.tile
local sceneryLine = 78
local grassTop = 70
local coinSize = 12
local emblemSize = 14
local counterPadding = 6
local counterGap = 10
local iconGap = 4
local narrowestCounters = 128
local outline = "#161c2e"
local track = "#383032"
local skyBand = 4
local skyDepth = grassTop + 8
local nightTop = "#0b1330"
local nightHorizon = "#1f2f5c"
local nightShade = "#060d24"
local nightDepth = 0.4
local dayClouds = 0.9
local nightClouds = 0.45
local starlight = "#fff4d6"
local moonlight = "#f4eed4"
local crater = "#ddd5b6"

local function channel(color, index)
    return tonumber(color:sub(index * 2, index * 2 + 1), 16)
end

-- The night sky darkens from the horizon up in bands of four units, as pixel art shades a sky, so the color of each band is worked out once.
local nightSky = {}

for top = 0, skyDepth - skyBand, skyBand do
    local share = top / (skyDepth - skyBand)
    local mixed = {}

    for index = 1, 3 do
        mixed[index] = math.floor(channel(nightTop, index) + (channel(nightHorizon, index) - channel(nightTop, index)) * share + 0.5)
    end

    nightSky[#nightSky + 1] = { top = top, color = string.format("#%02x%02x%02x", mixed[1], mixed[2], mixed[3]) }
end

-- The stars keep their places in the sky of any width, about one every thirty-six units, and one in five shines larger.
local stars = {}

for index = 0, 95 do
    stars[#stars + 1] = { x = index * 36 + index * 7919 % 23, y = 4 + index * 104729 % 43, size = index % 5 == 0 and 2 or 1, beat = index * 2.1 }
end

local function frameOf(commands, sheet, frame, x, y, scale, extra)
    local command = { op = "image", image = sheet.image, x = x, y = y, width = sheet.width * scale, height = sheet.height * scale, source = { x = frame * sheet.width, y = 0, width = sheet.width, height = sheet.height } }

    for key, value in pairs(extra or {}) do
        command[key] = value
    end

    commands[#commands + 1] = command
end

-- A figure stands with its feet on the ground and its middle on its place, and a foe faces the knight coming from the left, mirrored about its own middle, because a picture turns within its rectangle.
local function figure(commands, sheet, frame, x, s, flip, opacity)
    local left = flip and sheet.width - sheet.centre or sheet.centre
    frameOf(commands, sheet, frame, (x - left * figureScale) * s, (adventure.ground - sheet.feet * figureScale) * s, figureScale * s, { flipX = flip, opacity = opacity })
end

-- A health bar floats just above the head of a figure, and never higher than above the tallest warrior, so a lance raised overhead leaves it where the head is.
local function barTop(look)
    return math.max(36, adventure.ground - look.idle.feet * figureScale - 4)
end

-- A bar is framed in the dark outline every figure of the pack is drawn with, so it reads over the sky, the grass and the figures alike.
local function bar(commands, x, y, width, part, color, s)
    local left = x - width / 2
    commands[#commands + 1] = { op = "rect", x = (left - 1) * s, y = (y - 1) * s, width = (width + 2) * s, height = 5 * s, color = outline, radius = 1.5 * s }
    commands[#commands + 1] = { op = "rect", x = left * s, y = y * s, width = width * s, height = 3 * s, color = track, radius = s }
    commands[#commands + 1] = { op = "rect", x = left * s, y = y * s, width = width * math.max(0, math.min(1, part)) * s, height = 3 * s, color = color, radius = s }
end

-- The grass edge stands on its line and plain grass fills the band below it to the bottom of the canvas, both repeated from one tile each.
local function ground(commands, state, canvas, s)
    local left = -state.groundShift * s
    local width = canvas.width - left
    local tile = { width = tileSize * s, height = tileSize * s }
    local below = (grassTop + tileSize) * s

    commands[#commands + 1] = { op = "image", image = sheets.tilemap, x = left, y = grassTop * s, width = width, height = tileSize * s, source = sheets.grassEdge, tile = tile }
    commands[#commands + 1] = { op = "image", image = sheets.tilemap, x = left, y = below, width = width, height = math.max(1, canvas.height - below), source = sheets.grass, tile = tile }
end

-- The day keeps the sky of the pack, and the night sky darkens from the horizon up.
local function sky(commands, canvas, s, night)
    if night == nil then
        commands[#commands + 1] = { op = "image", image = sheets.sky, x = 0, y = 0, width = canvas.width, height = canvas.height }
        return
    end

    for _, band in ipairs(nightSky) do
        commands[#commands + 1] = { op = "rect", x = 0, y = band.top * s, width = canvas.width, height = skyBand * s, color = band.color }
    end

    commands[#commands + 1] = { op = "rect", x = 0, y = skyDepth * s, width = canvas.width, height = math.max(0, canvas.height - skyDepth * s), color = nightHorizon }
end

-- The stars of the night twinkle each on its own beat, and a full moon stands among them a little left of where the counters begin.
local function moonAndStars(commands, state, s, night, counterLeft)
    if night == nil then
        return
    end

    for _, star in ipairs(stars) do
        if star.x < state.width then
            commands[#commands + 1] = { op = "rect", x = star.x * s, y = star.y * s, width = star.size * s, height = star.size * s, color = starlight, opacity = 0.6 + 0.4 * math.sin(night.seconds * 1.3 + star.beat) }
        end
    end

    local x = math.max(24, math.min(state.width * 0.5, counterLeft - 34))
    commands[#commands + 1] = { op = "circle", x = x * s, y = 17 * s, radius = 10 * s, color = moonlight, opacity = 0.12 }
    commands[#commands + 1] = { op = "circle", x = x * s, y = 17 * s, radius = 5 * s, color = moonlight }
    commands[#commands + 1] = { op = "circle", x = (x - 1.5) * s, y = 16 * s, radius = 1.2 * s, color = crater }
    commands[#commands + 1] = { op = "circle", x = (x + 2) * s, y = 18.5 * s, radius = 0.8 * s, color = crater }
end

local function landscape(commands, state, canvas, s, night, counterLeft)
    sky(commands, canvas, s, night)
    moonAndStars(commands, state, s, night, counterLeft)

    -- Clouds are fainter at night, since only the moon lights them.
    local cloudy = night == nil and dayClouds or nightClouds

    for _, cloud in ipairs(state.clouds) do
        commands[#commands + 1] = { op = "image", image = cloud.look.image, x = cloud.x * s, y = cloud.y * s, width = cloud.look.width * cloudScale * s, height = cloud.look.height * cloudScale * s, opacity = cloudy }
    end

    ground(commands, state, canvas, s)

    -- Trees, bushes and rocks stand on a line behind the path and a little smaller, so every figure passes in front of them.
    for _, item in ipairs(state.scenery) do
        local sheet = sheets.scenery[item.look]
        frameOf(commands, sheet, sheets.frame(sheet, item.swing, true), (item.x - sheet.centre * sceneryScale) * s, (sceneryLine - sheet.feet * sceneryScale) * s, sceneryScale * s)
    end
end

-- The picture of an item rests with its bottom on the ground, an emblem showing the kind of warrior it holds.
local function items(commands, state, s)
    for _, item in ipairs(state.items) do
        local look = sheets.items[item.kind == "emblem" and item.class or item.kind]
        local height = itemSize * look.height / math.max(look.width, look.height)
        local width = itemSize * look.width / math.max(look.width, look.height)
        commands[#commands + 1] = { op = "image", image = look.image, x = (item.x - width / 2) * s, y = (item.y - height) * s, width = width * s, height = height * s }
    end
end

-- An arrow is turned along its flight, and one flying to the left is its picture mirrored and turned half a circle back.
local function arrow(commands, flying, s)
    local left = flying.to < flying.from
    local rotation = left and flying.angle % (2 * math.pi) - math.pi or flying.angle
    commands[#commands + 1] = { op = "image", image = sheets.arrow, x = (flying.x - 11) * s, y = (flying.y - 11) * s, width = 22 * s, height = 22 * s, flipX = left, rotation = rotation }
end

local function foes(commands, bars, state, s)
    for _, foe in ipairs(state.foes) do
        local look = sheets.foes[foe.kind]
        local sheet = look[foe.action] or look.idle
        local frame = sheets.frame(sheet, foe.swing, true)

        if foe.action == "attack" then
            sheet, frame = sheets.strike(look.attack, look.idle, foe.swing)
        end

        figure(commands, sheet, frame, foe.x, s, true, 1)
        bar(bars, foe.x, barTop(look), 18, foe.health / foe.most, "#d9534f", s)
    end

    for _, list in ipairs({ state.arrows, state.shots }) do
        for _, flying in ipairs(list) do
            arrow(commands, flying, s)
        end
    end
end

-- The knight is drawn as the warrior he is now, striking with the attack of that warrior and guarding while he rests.
local function hero(commands, bars, state, s)
    local knight = state.hero
    local look = sheets.classes[state.progress.class]
    local x = adventure.heroX()
    local sheet = look.run
    local frame = sheets.frame(sheet, knight.swing, true)

    if knight.action == "attack" then
        sheet, frame = sheets.strike(sheets.heroAttack(state.progress.class, knight.strikes), look.idle, knight.swing)
    elseif knight.action == "guard" then
        sheet = look.guard or look.idle
        frame = sheets.frame(sheet, knight.swing, true)
    end

    figure(commands, sheet, frame, x, s, false, knight.resting > 0 and 0.45 or 1)
    bar(bars, x, barTop(look), 20, knight.health / adventure.health(state.progress.level), "#5cb85c", s)
end

local function effects(commands, state, s)
    for _, shown in ipairs(state.effects) do
        local sheet = sheets.effects[shown.kind]
        local scale = shown.kind == "smoke" and 0.9 or 0.5
        frameOf(commands, sheet, sheets.frame(sheet, shown.time, false), (shown.x - sheet.centre * scale) * s, (shown.y - sheet.feet * scale) * s, scale * s)
    end
end

-- A coin flies in an arc from where it was won to the counter of gold.
local function coins(commands, state, s, target)
    for _, coin in ipairs(state.coins) do
        local progress = math.max(0, coin.time) / 0.8
        local x = coin.x + (target.x - coin.x) * progress
        local y = coin.y + (target.y - coin.y) * progress - math.sin(progress * math.pi) * 24
        commands[#commands + 1] = { op = "image", image = sheets.coin, x = (x - coinSize / 2) * s, y = (y - coinSize / 2) * s, width = coinSize * s, height = coinSize * s, opacity = coin.time < 0 and 0 or 1 }
    end
end

-- The counters stand on the right: the emblem of the warrior the knight is, the gold, the level and the experience toward the next level.
-- Each one is placed from the width of the texts before it with a gap between them, and their box widens to hold them all.
function drawing.counters(state, s)
    local size = math.max(7, math.min(40, 11 * s))
    local progress = state.progress
    local gold = text("task-hero.band.gold", workpane.i18n.number(progress.gold))
    local level = text("task-hero.band.level", progress.level)
    local goldWidth = workpane.ui.textWidth(gold, { size = size, face = "semibold" }) / s
    local levelWidth = workpane.ui.textWidth(level, { size = size, face = "semibold" }) / s
    local width = math.max(narrowestCounters, counterPadding + emblemSize + counterGap + coinSize + iconGap + goldWidth + counterGap + levelWidth + counterPadding)
    local right = state.width - 8
    local left = right - width
    local coin = left + counterPadding + emblemSize + counterGap

    return { size = size, gold = gold, level = level, left = left, right = right, width = width, emblem = left + counterPadding, coin = coin, goldAt = coin + coinSize + iconGap, goldWidth = goldWidth, levelAt = right - counterPadding, levelWidth = levelWidth }
end

local function counters(commands, state, s, laid)
    local progress = state.progress
    local top = adventure.hud + 3

    commands[#commands + 1] = { op = "rect", x = laid.left * s, y = adventure.hud * s, width = laid.width * s, height = 30 * s, color = "#1f1a16", opacity = 0.55, radius = 5 * s }
    commands[#commands + 1] = { op = "image", image = sheets.items[progress.class].image, x = laid.emblem * s, y = top * s, width = emblemSize * s, height = emblemSize * s }
    commands[#commands + 1] = { op = "image", image = sheets.coin, x = laid.coin * s, y = top * s, width = coinSize * s, height = coinSize * s }
    commands[#commands + 1] = { op = "text", x = laid.goldAt * s, y = top * s, text = laid.gold, color = "#ffe38a", size = laid.size, face = "semibold" }
    commands[#commands + 1] = { op = "text", x = laid.levelAt * s, y = top * s, text = laid.level, color = "#ffffff", size = laid.size, face = "semibold", align = "end" }
    bar(commands, laid.left + laid.width / 2, adventure.hud + 21, laid.width - 2 * counterPadding, progress.experience / adventure.needed(progress.level), "#e6c35c", s)
end

-- Answers the commands that draw the adventure on a band of the given size, with or without its counters, by day or in a night the clock gives.
-- The shade of the night lies over the world and under the health bars and the counters, which stay as readable at night as by day.
function drawing.commands(state, canvas, showCounters, night)
    local s = canvas.height / adventure.height
    local commands = {}
    local bars = {}
    local laid = showCounters and drawing.counters(state, s) or nil

    landscape(commands, state, canvas, s, night, laid ~= nil and laid.left or state.width - 8 - narrowestCounters)
    items(commands, state, s)
    foes(commands, bars, state, s)
    hero(commands, bars, state, s)
    effects(commands, state, s)

    if night ~= nil then
        commands[#commands + 1] = { op = "rect", x = 0, y = 0, width = canvas.width, height = canvas.height, color = nightShade, opacity = nightDepth }
    end

    table.move(bars, 1, #bars, #commands + 1, commands)

    if laid ~= nil then
        coins(commands, state, s, { x = laid.coin + coinSize / 2, y = adventure.hud + 9 })
        counters(commands, state, s, laid)
    end

    if state.paused then
        commands[#commands + 1] = { op = "rect", x = 0, y = 0, width = canvas.width, height = canvas.height, color = "#000000", opacity = 0.3 }
        commands[#commands + 1] = { op = "text", x = canvas.width / 2, y = canvas.height / 2 - 8 * s, text = text("task-hero.band.paused"), color = "#ffffff", size = math.max(8, math.min(48, 14 * s)), face = "semibold", align = "center" }
    end

    return commands
end

return drawing
