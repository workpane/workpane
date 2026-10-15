local bridge = require("workpane.bridge")
local resources = require("workpane.resources")
local task = require("workpane.task")
local workspace = require("workpane.workspace")

local picker = {}

function picker.open(owner, options, dialogs, workpane)
    if options.isCurrent ~= nil and type(options.isCurrent) ~= "function" then
        bridge.raise("ui_folder_selection_invalid", "A folder selector checks its current owner with a function", "")
    end

    local scope = resources.scope(owner)

    local function acquire()
        return workspace.open(owner, options.workspace, workpane)
    end

    local function release(backend)
        return backend.close()
    end

    local accepted, chosen = pcall(function()
        local backend = task.await(scope:acquire(acquire, release))

        if options.isCurrent ~= nil and not options.isCurrent() then
            return nil
        end

        return task.await(dialogs.selectFolder({ title = options.title, initial = options.initial, backend = backend }))
    end)

    local _, cleanup = scope:close():await()

    if not accepted or cleanup ~= nil then
        error(not accepted and chosen or cleanup, 0)
    end

    return chosen
end

return picker
