-- Keeps the progress of the knight written without a write for every victory, at most once every ten seconds of adventure and at once when it pauses or the plugin stops.
local progress = {}

local interval = 10

-- A keeper writes the progress through the function it is given, which answers the future of the write.
function progress.keeper(write)
    return { write = write, elapsed = 0, unkept = nil }
end

-- The progress changed and waits for its turn to be written.
function progress.changed(keeper, value)
    keeper.unkept = value
end

-- Advances the time of the adventure and answers the future of a write once its turn came.
function progress.tick(keeper, delta)
    keeper.elapsed = keeper.elapsed + delta

    if keeper.unkept == nil or keeper.elapsed < interval then
        return nil
    end

    return progress.flush(keeper)
end

-- Writes what waits at once and answers the future of the write, or nothing when all was already written.
function progress.flush(keeper)
    local value = keeper.unkept

    if value == nil then
        return nil
    end

    keeper.unkept = nil
    keeper.elapsed = 0

    return keeper.write(value)
end

-- A progress set back from elsewhere replaces whatever was waiting.
function progress.forget(keeper)
    keeper.unkept = nil
    keeper.elapsed = 0
end

return progress
