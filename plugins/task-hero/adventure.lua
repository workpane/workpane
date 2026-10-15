-- The rules of the adventure in a world 96 units tall: a knight stands his ground near the left edge and walks forever, foes come in from beyond the right edge, every blow lands on the frame of its attack, what falls to the ground waits until he picks it up, and whatever leaves the band on the left leaves the adventure.
local sheets = include("sheets")

local adventure = {}

adventure.height = 96
adventure.ground = 86
adventure.hud = 4
adventure.figureScale = 0.5
adventure.sceneryScale = 0.28
adventure.cloudScale = 86 / 576
adventure.itemSize = 14
adventure.tile = 24

local heroPlace = 110
local walkSpeed = 36
local foeSpeed = 40
local sheepSpeed = 12
local enterMargin = 40
local mostFoes = 3
local foeSpacing = { 160, 320 }
local queueGap = 2
local foeInterval = 1.1
local arrowSpeed = 400
local arrowArc = 0.25
local shortestFlight = 0.12
local bowHeight = 17
local coinSeconds = 0.8
local restSeconds = 2.5
local sceneryGap = { 26, 90 }
local cloudGap = { 150, 260 }
local cloudDrift = 0.15
local gravity = 220
local itemBounce = 0.35
local fallFrom = -12
local pickupReach = 6
local mostItems = 2
local dropChance = 0.2
local itemGap = 72
local fallDistance = 240
local supplyDistance = { 700, 1400 }
local landingShare = { 0.5, 0.65 }
local landingNearest = 160
local landingMargin = 40
local pouchGold = 8
local emblemGold = 10
local drops = { { kind = "meat", weight = 35 }, { kind = "pouch", weight = 40 }, { kind = "emblem", weight = 25 } }
local supplies = { { kind = "meat", weight = 20 }, { kind = "pouch", weight = 30 }, { kind = "emblem", weight = 50 } }

local function copy(progress)
    return { gold = progress.gold, level = progress.level, experience = progress.experience, victories = progress.victories, class = progress.class }
end

-- The knight gains health and strength with each level, and each level asks for a little more experience than the last.
function adventure.health(level)
    return 60 + 12 * (level - 1)
end

function adventure.damage(level)
    return 8 + 2 * (level - 1)
end

function adventure.needed(level)
    return math.floor(20 * level ^ 1.35)
end

-- Answers how the knight fights as the warrior he is now: his reach, the seconds between his strikes, the damage of a strike and whether he shoots.
function adventure.fight(state)
    local fight = sheets.classes[state.progress.class].fight

    return { reach = fight.reach, interval = fight.interval, damage = math.floor(adventure.damage(state.progress.level) * fight.strength + 0.5), shoots = fight.shoots == true }
end

local function between(state, range)
    return range[1] + state.random() * (range[2] - range[1])
end

function adventure.new(progress, random)
    local state = {
        random = random,
        progress = copy(progress),
        width = 400,
        hero = { health = adventure.health(progress.level), action = "run", swing = 0, cooldown = 0, strikes = 0, resting = 0 },
        foes = {},
        arrows = {},
        shots = {},
        effects = {},
        coins = {},
        items = {},
        scenery = {},
        clouds = {},
        groundShift = 0,
        paused = false,
        calm = 0,
    }

    state.supply = between(state, supplyDistance)
    state.spacing = between(state, foeSpacing)

    return state
end

-- The knight stands at a fixed distance from the left edge, so a resize only widens or narrows the map ahead of him.
function adventure.heroX()
    return heroPlace
end

function adventure.resize(state, width)
    state.width = width
end

function adventure.pause(state)
    state.paused = not state.paused
end

-- An effect on the ground moves with it while the knight walks, and one on the knight stays with him.
local function effect(state, kind, x, y, grounded)
    state.effects[#state.effects + 1] = { kind = kind, x = x, y = y, time = 0, grounded = grounded }
end

local function weighted(state, choices)
    local total = 0

    for _, choice in ipairs(choices) do
        total = total + choice.weight
    end

    local pick = state.random() * total

    for _, choice in ipairs(choices) do
        pick = pick - choice.weight

        if pick <= 0 then
            return choice.kind
        end
    end

    return choices[#choices].kind
end

-- Answers where the next item lands: a little past the middle of the map and a few steps ahead of the knight, a gap past every item already there and short of the right edge, or nothing when no spot is left.
function adventure.landing(state)
    local last = state.width - landingMargin
    local spot = math.min(last, math.max(adventure.heroX() + landingNearest, state.width * between(state, landingShare)))

    for _, item in ipairs(state.items) do
        spot = math.max(spot, item.x + itemGap)
    end

    if spot > last then
        return nil
    end

    return spot
end

-- Sets an item falling from the top of the band onto a spot, where an emblem shows one of the three kinds of warrior.
function adventure.drop(state, kind, x)
    local item = { kind = kind, x = x, y = fallFrom, velocity = 0, bounced = false, resting = false }

    if kind == "emblem" then
        item.class = sheets.classNames[math.floor(state.random() * #sheets.classNames) + 1]
    end

    state.items[#state.items + 1] = item
end

local function coinsFrom(state, x, y, count)
    for index = 1, count do
        state.coins[#state.coins + 1] = { x = x, y = y, time = -0.08 * (index - 1) }
    end
end

local function ahead(foe)
    return foe.health > 0 and foe.x > adventure.heroX() - 4
end

-- A foe is chosen by weight, grows with the level of the knight so the road stays a match for him, and enters from beyond the right edge.
local function spawnFoe(state)
    local total = 0
    local kinds = {}

    for kind, foe in pairs(sheets.foes) do
        kinds[#kinds + 1] = kind
        total = total + foe.weight
    end

    table.sort(kinds)
    local pick = state.random() * total
    local chosen = kinds[#kinds]

    for _, kind in ipairs(kinds) do
        pick = pick - sheets.foes[kind].weight

        if pick <= 0 then
            chosen = kind
            break
        end
    end

    local foe = sheets.foes[chosen]
    local growth = state.progress.level - 1
    local health = math.floor(foe.health * (1 + 0.18 * growth))
    state.foes[#state.foes + 1] = { kind = chosen, x = state.width + enterMargin, health = health, most = health, damage = math.floor(foe.damage * (1 + 0.12 * growth) + 0.5), action = "run", swing = state.random() * 2, cooldown = 0 }
    state.spacing = between(state, foeSpacing)
end

-- A new foe enters once the last one walked far enough into the band, and never more than a few are in the band at once.
local function keepFoes(state)
    local count = 0
    local farthest = -math.huge

    for _, foe in ipairs(state.foes) do
        if ahead(foe) then
            count = count + 1
            farthest = math.max(farthest, foe.x)
        end
    end

    if count < mostFoes and farthest <= state.width - state.spacing then
        spawnFoe(state)
    end
end

local function keepScenery(state)
    local last = state.scenery[#state.scenery]
    local x = last ~= nil and last.x or 0

    while x < state.width + 60 do
        x = x + between(state, sceneryGap)
        state.scenery[#state.scenery + 1] = { x = x, look = math.floor(state.random() * #sheets.scenery) + 1, swing = state.random() * 2 }
    end
end

-- Clouds follow one another across the sky from the left edge to just past the right one, each one of the pictures of the pack.
local function keepClouds(state)
    local last = state.clouds[#state.clouds]
    local x = last ~= nil and last.x or -cloudGap[1]

    while x < state.width + enterMargin do
        x = x + between(state, cloudGap)
        state.clouds[#state.clouds + 1] = { x = x, y = 2 + state.random() * 14, look = sheets.clouds[math.floor(state.random() * #sheets.clouds) + 1] }
    end
end

-- An item falls only once the knight walked far enough since the last one, while the map holds fewer than the most items and keeps a free spot for it, so items never pile up while he fights.
local function release(state, choices)
    if state.calm > 0 or #state.items >= mostItems then
        return false
    end

    local spot = adventure.landing(state)

    if spot == nil then
        return false
    end

    adventure.drop(state, weighted(state, choices), spot)
    state.calm = fallDistance

    return true
end

-- Something falls from the sky each time the knight has walked the distance the next supply waits for.
local function supply(state)
    if state.supply > 0 or not release(state, supplies) then
        return
    end

    state.supply = between(state, supplyDistance)
end

-- The world moves toward the knight while he walks, the clouds more slowly than the ground, and every step brings the next item closer.
local function walk(state, seconds)
    local shift = walkSpeed * seconds
    state.groundShift = (state.groundShift + shift) % adventure.tile
    state.supply = state.supply - shift
    state.calm = math.max(0, state.calm - shift)

    for _, list in ipairs({ state.foes, state.scenery, state.items }) do
        for _, item in ipairs(list) do
            item.x = item.x - shift
        end
    end

    for _, list in ipairs({ state.arrows, state.shots }) do
        for _, arrow in ipairs(list) do
            arrow.from = arrow.from - shift
            arrow.to = arrow.to - shift
        end
    end

    for _, shown in ipairs(state.effects) do
        if shown.grounded then
            shown.x = shown.x - shift
        end
    end

    for _, cloud in ipairs(state.clouds) do
        cloud.x = cloud.x - shift * cloudDrift
    end
end

local function nearest(state)
    local found

    for _, foe in ipairs(state.foes) do
        if ahead(foe) and (found == nil or foe.x < found.x) then
            found = foe
        end
    end

    return found
end

-- A beaten foe vanishes in a puff of smoke with the blow it was still swinging, drops gold that flies to the counter and sometimes an item that falls ahead, a sheep leaves meat that fills the health of the knight in a glow of healing, and the experience may bring a level.
local function defeat(state, foe, events)
    local reward = sheets.foes[foe.kind]
    foe.blow = nil
    effect(state, "smoke", foe.x, adventure.ground - 20, true)
    coinsFrom(state, foe.x, adventure.ground - 26, reward.gold)

    if reward.meat then
        state.hero.health = adventure.health(state.progress.level)
        effect(state, "heal", adventure.heroX(), adventure.ground, false)
    elseif state.random() < dropChance then
        release(state, drops)
    end

    state.progress.victories = state.progress.victories + 1
    state.progress.experience = state.progress.experience + reward.experience
    events[#events + 1] = "victory"

    while state.progress.experience >= adventure.needed(state.progress.level) do
        state.progress.experience = state.progress.experience - adventure.needed(state.progress.level)
        state.progress.level = state.progress.level + 1
        state.hero.health = adventure.health(state.progress.level)
        effect(state, "heal", adventure.heroX(), adventure.ground, false)
        events[#events + 1] = "level"
    end
end

local function hurt(state, damage, events)
    local hero = state.hero

    if hero.resting > 0 then
        return
    end

    hero.health = hero.health - damage

    if hero.health > 0 then
        return
    end

    -- A fallen knight rests a moment and rises again, and the foe that beat him leaves in a puff of smoke.
    hero.health = 0
    hero.resting = restSeconds
    effect(state, "smoke", adventure.heroX(), adventure.ground - 20, false)
    local foe = nearest(state)

    if foe ~= nil then
        effect(state, "smoke", foe.x, adventure.ground - 20, true)
        foe.health = 0
        foe.blow = nil
    end

    events[#events + 1] = "fall"
end

local function wound(state, foe, damage, events)
    if foe.health <= 0 then
        return
    end

    foe.health = foe.health - damage

    if foe.health <= 0 then
        defeat(state, foe, events)
    end
end

-- An arrow rises from the bow that loosed it and comes down on the spot its target stood on at that moment, a quarter of the way as high as it is far, turning along its path.
local function travel(arrow, seconds)
    arrow.time = arrow.time + seconds
    local share = math.min(1, arrow.time / arrow.flight)
    local distance = arrow.to - arrow.from
    local rise = math.abs(distance) * arrowArc
    arrow.x = arrow.from + distance * share
    arrow.y = adventure.ground - bowHeight - 4 * rise * share * (1 - share)
    arrow.angle = math.atan(-4 * rise * (1 - 2 * share), distance)

    return arrow.time >= arrow.flight
end

-- The blow of a strike lands on the impact frame of its attack: a sword or a lance wounds its target and an archer looses an arrow at the spot its target stands on.
local function landBlow(state, figure, events)
    local blow = figure.blow

    if blow == nil or figure.swing < blow.at then
        return
    end

    figure.blow = nil

    if blow.shoots then
        local list = blow.target ~= nil and state.shots or state.arrows
        local to = blow.target ~= nil and blow.target.x or adventure.heroX()
        local arrow = { from = blow.from, to = to, time = 0, flight = math.max(shortestFlight, math.abs(to - blow.from) / arrowSpeed), damage = blow.damage, target = blow.target }
        travel(arrow, 0)
        list[#list + 1] = arrow
        return
    end

    if blow.target ~= nil then
        wound(state, blow.target, blow.damage, events)
        return
    end

    hurt(state, blow.damage, events)
end

-- The knight strikes the moment a foe comes within his reach and then once each interval, and plays every attack to its last frame before he walks or strikes again.
local function heroTurn(state, seconds, events)
    local hero = state.hero
    local fighting = adventure.fight(state)
    hero.cooldown = math.max(0, hero.cooldown - seconds)
    landBlow(state, hero, events)

    if hero.action == "attack" and hero.swing < sheets.duration(sheets.heroAttack(state.progress.class, hero.strikes)) then
        return
    end

    local foe = nearest(state)

    if foe == nil or foe.x - adventure.heroX() > fighting.reach then
        hero.action = "run"
        walk(state, seconds)
        return
    end

    hero.action = "attack"

    if hero.cooldown > 0 then
        return
    end

    hero.strikes = hero.strikes + 1
    hero.swing = 0
    hero.cooldown = fighting.interval
    hero.blow = { at = sheets.impact(sheets.heroAttack(state.progress.class, hero.strikes)), target = foe, damage = fighting.damage, shoots = fighting.shoots, from = adventure.heroX() + 8 }
end

-- Answers where a foe stops behind the one ahead of it in the line, where the pictures of the two waiting foes no longer overlap.
local function behind(ahead, foe)
    local front = sheets.foes[ahead.kind].idle
    local back = sheets.foes[foe.kind].idle

    return ahead.x + (front.centre + back.width - back.centre) * adventure.figureScale + queueGap
end

-- Foes walk in a line toward the knight, each stopping at its reach or behind the foe ahead of it, and strike him once he stands within their reach.
local function foesTurn(state, seconds, events)
    local line = {}

    for _, foe in ipairs(state.foes) do
        foe.cooldown = math.max(0, foe.cooldown - seconds)
        landBlow(state, foe, events)

        if ahead(foe) then
            line[#line + 1] = foe
        else
            foe.action = "idle"
        end
    end

    table.sort(line, function(first, second)
        return first.x < second.x
    end)

    local previous

    for _, foe in ipairs(line) do
        local reward = sheets.foes[foe.kind]
        local stop = adventure.heroX() + reward.reach

        if previous ~= nil then
            stop = math.max(stop, behind(previous, foe))
        end

        local striking = foe.action == "attack" and reward.attack ~= nil and foe.swing < sheets.duration(reward.attack)

        if not striking then
            if foe.x > stop then
                foe.action = "run"
                foe.x = math.max(stop, foe.x - (reward.meat and sheepSpeed or foeSpeed) * seconds)
            elseif foe.x - adventure.heroX() > reward.reach or reward.damage == 0 or state.hero.resting > 0 then
                foe.action = "idle"
            else
                foe.action = "attack"

                if foe.cooldown == 0 then
                    foe.swing = 0
                    foe.cooldown = foeInterval
                    foe.blow = { at = sheets.impact(reward.attack), damage = foe.damage, shoots = reward.arrows == true, from = foe.x - 8 }
                end
            end
        end

        previous = foe
    end
end

-- The arrows of the foes wound the knight and the arrows of the knight the foe he aimed at as they come down.
local function fly(state, seconds, events)
    local arrows = {}
    local shots = {}

    for _, arrow in ipairs(state.arrows) do
        if travel(arrow, seconds) then
            hurt(state, arrow.damage, events)
        else
            arrows[#arrows + 1] = arrow
        end
    end

    for _, shot in ipairs(state.shots) do
        if travel(shot, seconds) then
            wound(state, shot.target, shot.damage, events)
        else
            shots[#shots + 1] = shot
        end
    end

    state.arrows = arrows
    state.shots = shots
end

-- Items fall under gravity, bounce once and rest on the ground.
local function fall(state, seconds)
    for _, item in ipairs(state.items) do
        if not item.resting then
            item.velocity = item.velocity + gravity * seconds
            item.y = item.y + item.velocity * seconds

            if item.y >= adventure.ground then
                item.y = adventure.ground
                item.resting = item.bounced or item.velocity < 40
                item.velocity = -item.velocity * itemBounce
                item.bounced = true
            end
        end
    end
end

-- The knight picks up every resting item he reaches: meat fills his health, a pouch holds gold, and an emblem turns him into its kind of warrior or is worth gold when he already is one.
local function pick(state, item, events)
    local hero = state.hero
    local x = adventure.heroX()

    if item.kind == "meat" then
        hero.health = adventure.health(state.progress.level)
        effect(state, "heal", x, adventure.ground, false)
    elseif item.kind == "pouch" or item.class == state.progress.class then
        coinsFrom(state, item.x, item.y - 8, item.kind == "pouch" and pouchGold or emblemGold)
    else
        state.progress.class = item.class
        hero.action = "run"
        hero.strikes = 0
        hero.blow = nil
        effect(state, "heal", x, adventure.ground, false)
        events[#events + 1] = "class"
    end

    events[#events + 1] = "item"
end

local function gather(state, events)
    local kept = {}
    local reach = adventure.heroX() + pickupReach

    for _, item in ipairs(state.items) do
        if item.resting and item.x <= reach and state.hero.resting == 0 then
            pick(state, item, events)
        else
            kept[#kept + 1] = item
        end
    end

    state.items = kept
end

-- Whatever has wholly left the band on the left leaves the adventure without a reward, so a foe, an item, a piece of scenery or a cloud is forgotten once it can no longer be seen, and finished effects, landed coins and beaten foes go with them, the coins telling the gold they brought so it is kept.
local function settle(state, seconds, events)
    local effects = {}
    local coins = {}
    local foes = {}
    local items = {}
    local scenery = {}
    local clouds = {}

    for _, shown in ipairs(state.effects) do
        shown.time = shown.time + seconds

        if shown.time < sheets.duration(sheets.effects[shown.kind]) then
            effects[#effects + 1] = shown
        end
    end

    local landed = 0

    for _, coin in ipairs(state.coins) do
        coin.time = coin.time + seconds

        if coin.time >= coinSeconds then
            landed = landed + 1
        else
            coins[#coins + 1] = coin
        end
    end

    if landed > 0 then
        state.progress.gold = state.progress.gold + landed
        events[#events + 1] = "gold"
    end

    for _, foe in ipairs(state.foes) do
        if foe.health > 0 and foe.x + sheets.foes[foe.kind].span * adventure.figureScale > 0 then
            foes[#foes + 1] = foe
        end
    end

    for _, item in ipairs(state.items) do
        if item.x + adventure.itemSize / 2 > 0 then
            items[#items + 1] = item
        end
    end

    for _, piece in ipairs(state.scenery) do
        if piece.x + sheets.span(sheets.scenery[piece.look]) * adventure.sceneryScale > 0 then
            scenery[#scenery + 1] = piece
        end
    end

    for _, cloud in ipairs(state.clouds) do
        if cloud.x + cloud.look.width * adventure.cloudScale > 0 then
            clouds[#clouds + 1] = cloud
        end
    end

    state.effects = effects
    state.coins = coins
    state.foes = foes
    state.items = items
    state.scenery = scenery
    state.clouds = clouds
end

-- Advances the adventure by the seconds of a tick at the chosen speed and answers what changed the progress, so it can be kept.
function adventure.step(state, seconds, speed)
    local events = {}

    if state.paused then
        return events
    end

    local elapsed = seconds * speed
    local hero = state.hero
    hero.swing = hero.swing + elapsed

    for _, list in ipairs({ state.foes, state.scenery }) do
        for _, item in ipairs(list) do
            item.swing = item.swing + elapsed
        end
    end

    keepScenery(state)
    keepClouds(state)
    keepFoes(state)
    supply(state)

    if hero.resting > 0 then
        hero.resting = math.max(0, hero.resting - elapsed)
        hero.action = "guard"
        hero.blow = nil

        if hero.resting == 0 then
            hero.health = adventure.health(state.progress.level)
            effect(state, "heal", adventure.heroX(), adventure.ground, false)
        end
    else
        heroTurn(state, elapsed, events)
    end

    foesTurn(state, elapsed, events)
    fly(state, elapsed, events)
    fall(state, elapsed)
    gather(state, events)
    settle(state, elapsed, events)

    return events
end

return adventure
