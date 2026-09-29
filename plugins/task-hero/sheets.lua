-- The sprite sheets of the adventure: the file, the size of one frame in pixels, the frames it holds and their pace, where the middle and the feet of the figure stand in a frame, and for an attack the frame its blow lands on.
local sheets = {}

local function sheet(image, width, height, frames, fps, centre, feet, impact)
    return { image = image, width = width, height = height, frames = frames, fps = fps, centre = centre, feet = feet, impact = impact }
end

-- Every pose of a unit is cut from frames of one size in which the unit stands on the same pivot, the middle of its shadow, so the corner of each cut places the pose on that pivot and no pose shifts the figure.
local function pose(image, width, height, frames, fps, pivot, left, top, impact)
    return sheet(image, width, height, frames, fps, pivot.x - left, pivot.y - top, impact)
end

local soldier = { x = 96, y = 137 }
local spearman = { x = 160, y = 198 }
local flock = { x = 64, y = 81 }

local function warrior(color)
    return {
        idle = pose("units/" .. color .. "-warrior-idle.png", 84, 89, 8, 10, soldier, 59, 48),
        run = pose("units/" .. color .. "-warrior-run.png", 93, 91, 6, 12, soldier, 53, 46),
        attack = pose("units/" .. color .. "-warrior-attack.png", 120, 112, 4, 12, soldier, 43, 44, 2),
    }
end

local function lancer(color)
    return {
        idle = pose("units/" .. color .. "-lancer-idle.png", 70, 150, 12, 12, spearman, 114, 48),
        run = pose("units/" .. color .. "-lancer-run.png", 72, 149, 6, 12, spearman, 110, 54),
        attack = pose("units/" .. color .. "-lancer-attack.png", 186, 75, 3, 9, spearman, 121, 123, 1),
    }
end

local function archer(color)
    return {
        idle = pose("units/" .. color .. "-archer-idle.png", 72, 89, 6, 10, soldier, 56, 47),
        run = pose("units/" .. color .. "-archer-run.png", 73, 90, 4, 10, soldier, 57, 46),
        attack = pose("units/" .. color .. "-archer-shoot.png", 87, 90, 8, 16, soldier, 53, 46, 5),
    }
end

-- The knight takes one of three kinds of warrior, each with its reach, the seconds between its strikes, how much harder than a warrior it strikes and whether it shoots.
sheets.classes = {
    warrior = warrior("blue"),
    lancer = lancer("blue"),
    archer = archer("blue"),
}

sheets.classes.warrior.second = pose("units/blue-warrior-attack-2.png", 118, 104, 4, 12, soldier, 41, 48, 2)
sheets.classes.warrior.guard = pose("units/blue-warrior-guard.png", 70, 93, 6, 10, soldier, 57, 44)
sheets.classes.lancer.guard = pose("units/blue-lancer-guard.png", 153, 75, 6, 10, spearman, 122, 123)
sheets.classes.warrior.fight = { reach = 30, interval = 0.8, strength = 1 }
sheets.classes.lancer.fight = { reach = 46, interval = 1.1, strength = 1.5 }
sheets.classes.archer.fight = { reach = 120, interval = 1, strength = 0.8, shoots = true }
sheets.classNames = { "warrior", "lancer", "archer" }

-- Answers how far the wider side of a frame reaches from the middle of the figure, in pixels.
function sheets.span(sheet)
    return math.max(sheet.centre, sheet.width - sheet.centre)
end

-- Each foe has what it looks like while it waits, walks and strikes, how far its widest pose reaches from its middle, and the numbers of its fight, which grow with the level of the hero.
local function foe(look, numbers)
    for key, value in pairs(numbers) do
        look[key] = value
    end

    look.span = 0

    for _, key in ipairs({ "idle", "run", "attack" }) do
        if look[key] ~= nil then
            look.span = math.max(look.span, sheets.span(look[key]))
        end
    end

    return look
end

sheets.foes = {
    ["red-warrior"] = foe(warrior("red"), { health = 30, damage = 6, reach = 30, gold = 3, experience = 6, weight = 30 }),
    ["black-warrior"] = foe(warrior("black"), { health = 48, damage = 9, reach = 30, gold = 5, experience = 10, weight = 18 }),
    ["yellow-lancer"] = foe(lancer("yellow"), { health = 40, damage = 8, reach = 44, gold = 4, experience = 8, weight = 18 }),
    ["purple-archer"] = foe(archer("purple"), { health = 24, damage = 7, reach = 110, gold = 4, experience = 7, weight = 16, arrows = true }),
    sheep = foe({ idle = pose("units/sheep-idle.png", 45, 44, 6, 8, flock, 41, 40), run = pose("units/sheep-move.png", 52, 47, 4, 8, flock, 38, 37) }, { health = 12, damage = 0, reach = 26, gold = 0, experience = 2, weight = 18, meat = true }),
}

sheets.effects = {
    smoke = sheet("effects/smoke.png", 64, 57, 10, 16, 32, 28),
    heal = sheet("effects/heal.png", 98, 128, 11, 16, 43, 113),
}

sheets.scenery = {
    sheet("terrain/tree-1.png", 121, 190, 8, 6, 59, 189),
    sheet("terrain/tree-2.png", 108, 244, 8, 6, 53, 243),
    sheet("terrain/tree-3.png", 93, 147, 8, 6, 47, 146),
    sheet("terrain/tree-4.png", 81, 122, 8, 6, 40, 121),
    sheet("terrain/bush-1.png", 67, 46, 8, 6, 33, 45),
    sheet("terrain/bush-2.png", 46, 34, 8, 6, 23, 38),
    sheet("terrain/bush-3.png", 79, 59, 8, 6, 41, 58),
    sheet("terrain/bush-4.png", 46, 42, 8, 6, 24, 43),
    sheet("terrain/rock-1.png", 64, 64, 1, 1, 32, 50),
    sheet("terrain/rock-2.png", 64, 64, 1, 1, 32, 52),
    sheet("terrain/rock-3.png", 64, 64, 1, 1, 32, 51),
    sheet("terrain/rock-4.png", 64, 64, 1, 1, 32, 55),
}

sheets.clouds = {
    { image = "terrain/cloud-1.png", width = 495, height = 157 },
    { image = "terrain/cloud-2.png", width = 307, height = 110 },
    { image = "terrain/cloud-3.png", width = 165, height = 75 },
    { image = "terrain/cloud-4.png", width = 106, height = 57 },
}

-- The items a knight picks up from the ground, each with its picture and its size in pixels.
sheets.items = {
    meat = { image = "loot/meat.png", width = 64, height = 64 },
    pouch = { image = "loot/pouch.png", width = 24, height = 26 },
    warrior = { image = "loot/emblem-warrior.png", width = 48, height = 48 },
    lancer = { image = "loot/emblem-lancer.png", width = 48, height = 48 },
    archer = { image = "loot/emblem-archer.png", width = 48, height = 48 },
}

sheets.arrow = "units/arrow.png"
sheets.coin = "loot/coin.png"
sheets.sky = "terrain/sky.png"
sheets.tilemap = "terrain/tilemap.png"
sheets.grassEdge = { x = 64, y = 0, width = 64, height = 64 }
sheets.grass = { x = 64, y = 64, width = 64, height = 64 }

-- Answers every picture the adventure draws, which its canvas keeps ready from the start, so no figure is missing while its sheet is decoded the first time it shows.
function sheets.pictures()
    local found = {}
    local listed = {}

    local function visit(value)
        if type(value) == "string" and value:match("%.png$") and not found[value] then
            found[value] = true
            listed[#listed + 1] = value
        elseif type(value) == "table" then
            for _, inner in pairs(value) do
                visit(inner)
            end
        end
    end

    visit(sheets)
    table.sort(listed)

    return listed
end

-- Answers the frame of a sheet shown a number of seconds into its animation, looping or holding the last frame.
function sheets.frame(sheet, seconds, loops)
    local frame = math.floor(seconds * sheet.fps)

    if loops then
        return frame % sheet.frames
    end

    return math.min(frame, sheet.frames - 1)
end

-- Answers how many seconds one pass of a sheet lasts.
function sheets.duration(sheet)
    return sheet.frames / sheet.fps
end

-- Answers the attack of the knight as the warrior he is, where a warrior swings his second attack on every other strike.
function sheets.heroAttack(class, strikes)
    local look = sheets.classes[class]

    return strikes % 2 == 0 and look.second or look.attack
end

-- Answers the seconds from the start of an attack to the frame its blow lands on.
function sheets.impact(attack)
    return attack.impact / attack.fps
end

-- A figure that struck plays its attack once from the moment of the strike and then stands idle until the next one, so no attack ever holds its last frame.
function sheets.strike(attack, idle, seconds)
    local length = sheets.duration(attack)

    if seconds < length then
        return attack, sheets.frame(attack, seconds, false)
    end

    return idle, sheets.frame(idle, seconds - length, true)
end

return sheets
