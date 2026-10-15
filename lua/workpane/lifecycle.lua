-- Tracks each active owner and the product shutdown boundary.
local bridge = require("workpane.bridge")

local lifecycle = {}

local running = {}
local closing = false

function lifecycle.beginShutdown()
    closing = true
end

function lifecycle.closing()
    return closing
end

function lifecycle.start(owner)
    running[owner] = true
end

function lifecycle.withdraw(owner)
    running[owner] = nil
end

-- Refuses a call of an owner that is not running, which is what a definition that acts while it is loaded meets.
function lifecycle.check(owner)
    if not running[owner] then
        bridge.raise("plugin_not_running", "A plugin acted before it started or after it was withdrawn", owner)
    end
end

return lifecycle
