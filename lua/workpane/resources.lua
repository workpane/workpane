local bridge = require("workpane.bridge")
local lifecycle = require("workpane.lifecycle")
local task = require("workpane.task")

local resources = {}
local owners = {}
local states = setmetatable({}, { __mode = "k" })
local Scope = {}
local scopeType = { __metatable = "workpane.resources", __index = Scope, __newindex = function()
    bridge.raise("resources_read_only", "A resource scope cannot be changed", "")
end }

local function stateOf(scope)
    local state = states[scope]

    if state == nil then
        bridge.raise("resources_invalid", "The resource scope is unavailable", "")
    end

    return state
end

local function release(scope, entry)
    if entry.closing == nil then
        entry.closing = bridge.owned(scope, function()
            local accepted, value = pcall(function()
                return task.await(entry.release(entry.value))
            end)

            local state = stateOf(scope)
            state.entries[entry.value] = nil
            state.acquired = state.acquired - 1

            if not accepted then
                state.failure = state.failure or bridge.failure(value)
                error(value, 0)
            end

            return value
        end)
    end

    return entry.closing
end

function Scope:run(work)
    local state = stateOf(self)

    if type(work) ~= "function" then
        bridge.raise("resources_work_invalid", "A resource operation declares a function", "")
    end

    if state.closed then
        bridge.raise("resources_closed", "The resource scope is closing", "")
    end

    if state.count >= 64 then
        bridge.raise("resources_full", "The resource scope has reached its operation limit", "")
    end

    local operation = {}
    state.pending[operation] = true
    state.count = state.count + 1
    operation.future = bridge.owned(self, function()
        local accepted, value = pcall(work)
        state.pending[operation] = nil
        state.count = state.count - 1

        if not accepted then
            error(value, 0)
        end

        return value
    end)

    return operation.future
end

function Scope:acquire(open, close)
    if type(open) ~= "function" or type(close) ~= "function" then
        bridge.raise("resources_work_invalid", "Acquiring a resource declares opening and closing functions", "")
    end

    local state = stateOf(self)

    if state.acquired >= 64 then
        bridge.raise("resources_full", "The resource scope has reached its resource limit", "")
    end

    local pending = self:run(function()
        local accepted, value = pcall(function()
            return task.await(open())
        end)

        if not accepted then
            state.acquired = state.acquired - 1
            error(value, 0)
        end

        if value == nil or state.entries[value] ~= nil then
            state.acquired = state.acquired - 1
            bridge.raise("resources_value_invalid", "An acquired resource must have a distinct nonempty identity", "")
        end

        local entry = { value = value, release = close }
        state.entries[value] = entry

        if state.closed then
            task.await(release(self, entry))
            bridge.raise("resources_closed", "The resource scope is closing", "")
        end

        return value
    end)

    state.acquired = state.acquired + 1

    return pending
end

function Scope:release(value)
    local entry = stateOf(self).entries[value]

    if entry == nil then
        bridge.raise("resources_value_invalid", "The resource does not belong to this scope", "")
    end

    return release(self, entry)
end

function Scope:close()
    local state = stateOf(self)

    if bridge.owns(self) then
        bridge.raise("resources_close_in_operation", "A resource operation cannot close its own scope", "")
    end

    if state.closing ~= nil then
        return state.closing
    end

    state.closed = true
    state.closing = bridge.owned(self, function()
        local closing = {}
        local failure

        for _, entry in pairs(state.entries) do
            closing[#closing + 1] = release(self, entry)
        end

        for _, operation in ipairs(closing) do
            local _, problem = operation:await()
            failure = failure or problem
        end

        while next(state.pending) ~= nil do
            local operation = next(state.pending)
            operation.future:await()
        end

        local group = owners[state.owner]

        if group ~= nil then
            group[self] = nil

            if next(group) == nil then
                owners[state.owner] = nil
            end
        end

        failure = failure or state.failure

        if failure ~= nil then
            error(failure, 0)
        end

        return {}
    end)

    return state.closing
end

function resources.scope(owner)
    lifecycle.check(owner)
    local group = owners[owner] or {}
    local count = 0

    for _ in pairs(group) do
        count = count + 1
    end

    if count >= 64 then
        bridge.raise("resources_full", "The plugin has reached its resource scope limit", "")
    end

    local scope = setmetatable({}, scopeType)
    states[scope] = { owner = owner, entries = {}, pending = {}, count = 0, acquired = 0, closed = false }
    group[scope] = true
    owners[owner] = group

    return scope
end

function resources.forget(owner)
    for scope in pairs(owners[owner] or {}) do
        Scope.close(scope)
    end
end

return resources
