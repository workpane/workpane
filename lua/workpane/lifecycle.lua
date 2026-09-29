-- The owners that are running, so a plugin that is still loading, was refused or was withdrawn cannot act on the product or on other plugins.
local bridge = require("workpane.bridge")

local lifecycle = {}

local running = {}

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
