-- How the state of a task reads on a card, in its surface and in its conversation: the badge it wears and the phase its run is in.
local engine = include("engine")

local text = workpane.i18n.text

local status = {}

local badgeTones = { queued = "warning", running = "accent", succeeded = "success", failed = "danger", cancelled = "neutral", scheduled = "information", stopped = "warning", idle = "neutral" }

-- The badge says what the card last did: running or queued now, scheduled or idle before its first run, and otherwise how its last run ended.
function status.badge(task)
    local runState = engine.runState(task.id)
    local outcome = engine.outcome(task.id)
    local name

    if runState ~= "idle" then
        name = runState
    elseif outcome == nil then
        name = task.schedule ~= nil and task.schedule.enabled and "scheduled" or "idle"
    elseif outcome.status == "succeeded" then
        name = outcome.stopReason == "answered" and "succeeded" or "stopped"
    else
        name = outcome.status
    end

    return text("ai.badge." .. name), badgeTones[name]
end

-- A turn calling tools names the one it runs, or how many run together.
function status.phase(taskId)
    local phase = engine.phase(taskId)

    if phase ~= "calling-tool" then
        return text("ai.phase." .. phase)
    end

    local names = engine.runningTools(taskId)

    -- Before the calls of a turn are listed and after they finished, the turn only says it calls its tools.
    if #names == 0 then
        return text("ai.phase.calling-tools")
    end

    return text("ai.phase.calling-tool", #names == 1 and names[1] or text("ai.phase.tool-count", workpane.i18n.number(#names, 0)))
end

return status
