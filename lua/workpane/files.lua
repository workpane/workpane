local bridge = require("workpane.bridge")
local task = require("workpane.task")

local files = {}
local owners = {}
local states = setmetatable({}, { __mode = "k" })
local Stage = {}
local stageType = {
    __metatable = "workpane.staging",
    __index = function(stage, key)
        return key == "path" and states[stage].path or Stage[key]
    end,
    __newindex = function()
        bridge.raise("files_staging_read_only", "A private staging object cannot be changed", "")
    end,
}

function files.checkPath(path)
    if type(path) ~= "string" or path:find("%z") ~= nil or utf8.len(path) == nil then
        bridge.raise("files_path_invalid", "A file path must contain valid text without a NUL byte", "")
    end
end

function files.request(owner, name, argument, fields)
    return task.future(owner, function()
        for _, field in ipairs(fields) do
            files.checkPath(argument[field])
        end

        return task.await(bridge.request(name, argument))
    end)
end

function Stage:run(work)
    local state = states[self]

    if state == nil then
        bridge.raise("files_staging_invalid", "The private staging operation is unavailable", "")
    end

    if type(work) ~= "function" then
        bridge.raise("files_staging_work_invalid", "A staging operation declares a function that starts its file work", "")
    end

    if state.closed then
        bridge.raise("files_staging_closed", "The private staging directory is closing", "")
    end

    if state.count >= 64 then
        bridge.raise("files_staging_full", "The private staging directory has reached its operation limit", "")
    end

    local operation = {}
    state.pending[operation] = true
    state.count = state.count + 1
    operation.future = bridge.owned(self, function()
        local accepted, value = pcall(function()
            if state.closed then
                bridge.raise("files_staging_closed", "The private staging directory is closing", "")
            end

            return task.await(work())
        end)

        state.pending[operation] = nil
        state.count = state.count - 1

        if not accepted then
            error(value, 0)
        end

        return value
    end)

    return operation.future
end

function Stage:close()
    local state = states[self]

    if state == nil then
        bridge.raise("files_staging_invalid", "The private staging operation is unavailable", "")
    end

    if bridge.owns(self) then
        bridge.raise("files_staging_close_in_operation", "A staging operation cannot close its own directory", "")
    end

    if state.closing ~= nil then
        return state.closing
    end

    state.closed = true
    state.closing = bridge.owned(self, function()
        while next(state.pending) ~= nil do
            local operation = next(state.pending)
            operation.future:await()
        end

        local accepted, value = pcall(task.await, bridge.request("workpane_files_staging_close", { plugin = state.owner, id = state.id }))
        local group = owners[state.owner]

        if group ~= nil then
            group[self] = nil

            if next(group) == nil then
                owners[state.owner] = nil
            end
        end

        if not accepted then
            error(value, 0)
        end

        return value
    end)

    return state.closing
end

function files.staging(owner, path)
    files.checkPath(path)
    local opened = bridge.call("workpane_files_staging_open", { plugin = owner, path = path })
    local stage = setmetatable({}, stageType)
    local state = { owner = owner, id = opened.id, path = opened.path, pending = {}, count = 0, closed = false }
    states[stage] = state
    local group = owners[owner] or {}
    group[stage] = true
    owners[owner] = group
    local ready = bridge.request("workpane_files_staging_ready", { plugin = owner, id = opened.id })

    return bridge.future(function()
        local _, problem = ready:await()

        if problem ~= nil or state.closed then
            Stage.close(stage):await()
            error(problem or bridge.failure({ code = "files_staging_closed", message = "The private staging directory is closing", detail = "" }), 0)
        end

        return stage
    end)
end

function files.forget(owner)
    local group = owners[owner] or {}

    for stage in pairs(group) do
        Stage.close(stage)
    end
end

return files
