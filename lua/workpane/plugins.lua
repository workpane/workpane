-- The plugins the product discovered, the state of each one and the switch that starts and stops them while the product runs.
local async = require("async")
local bridge = require("workpane.bridge")
local events = require("workpane.events")
local http = require("workpane.http")
local lifecycle = require("workpane.lifecycle")
local loader = require("workpane.loader")
local logs = require("workpane.logs")
local preferences = require("workpane.preferences")
local process = require("workpane.process")
local reader = require("workpane.reader")
local sounds = require("workpane.sounds")
local ui = require("workpane.ui")

local plugins = {}

local stopGraceMilliseconds = 3000
local closingMilliseconds = 2500
local handoverMilliseconds = 20

local info
local entries = {}
local discovered = {}
local started = {}
local disabled = {}
local watchers = {}
local busy = false
local waiting = {}

-- The message a refusal is told with names what the reader can act on: the folder that cannot be read, the plugin already loaded or the file of the data.
local function refusalMessage(refusal)
    local code = refusal.failure.code

    if refusal.folder ~= nil then
        return "workpane.plugin.folder-unreadable", { refusal.folder }
    end

    if code == "plugin_duplicate" then
        return "workpane.plugin.duplicate", { refusal.id, refusal.failure.detail }
    end

    if code == "database_rows_invalid" or code == "database_schema_mismatch" then
        return "workpane.plugin.data-unreadable", { refusal.name or refusal.id, info.paths.database }
    end

    return "workpane.plugin.failed-message", { refusal.name or refusal.id }
end

-- A plugin or a folder that could not be loaded is told to the reader once, and the reason is written to the log for whoever looks.
local function tell(refusal)
    local failure = refusal.failure
    local key, args = refusalMessage(refusal)
    bridge.report("workpane", "plugins", "A plugin could not be loaded", { plugin = refusal.id, folder = refusal.folder, code = failure.code, message = failure.message, detail = failure.detail })
    bridge.call("workpane_notify", { plugin = "workpane", title = bridge.call("workpane_translate", { key = "workpane.plugin.failed-title" }), message = bridge.call("workpane_translate", { key = key, args = args }), severity = "error" })
end

local function announce()
    for _, watcher in ipairs(watchers) do
        local succeeded, failure = pcall(watcher)

        if not succeeded then
            bridge.report("workpane", "plugins", "A watcher of the plugins failed", { error = tostring(failure) })
        end
    end
end

-- Every change of the state of a plugin is written to the log with the state it left and the reason, so any case of its life can be followed in the Logs view.
local function enter(entry, state, reason)
    local previous = entry.state

    if previous == state then
        return
    end

    entry.state = state
    bridge.call("workpane_log", { plugin = "workpane", level = "info", category = "plugins", message = "A plugin changed its state", details = { plugin = entry.id, from = previous, to = state, reason = reason } })
    announce()
end

local function refuse(entry, failure)
    entry.failure = bridge.failure(failure)
    enter(entry, "refused", "It was refused with \"" .. entry.failure.code .. "\".")
    entry.requests = {}
    entry.attempt = false
    local name = entry.catalog and bridge.call("workpane_translate", { key = entry.candidate.definition.titleKey }) or nil
    tell({ id = entry.id, name = name, failure = entry.failure })
end

-- The catalog of a plugin is installed as soon as its code is read, so a plugin that does not run still has its title and description.
local function install(entry)
    entry.catalog = false

    if entry.candidate == nil then
        enter(entry, "refused", "It was refused with \"" .. (entry.failure and entry.failure.code or "plugin_unreadable") .. "\".")
        return
    end

    local installed, failure = pcall(bridge.call, "workpane_plugin_catalog", { plugin = entry.id, translations = entry.candidate.translations })

    if not installed then
        entry.candidate = nil
        entry.failure = bridge.failure(failure)
        enter(entry, "refused", "It was refused with \"" .. entry.failure.code .. "\".")
        return
    end

    entry.catalog = true
end

-- Reads a plugin from its folder again, so a plugin started once more never carries the state its modules kept from the run before.
local function reread(entry)
    local loaded, candidate = pcall(loader.load, entry.id, entry.directory, info)
    entry.candidate = loaded and candidate or nil
    entry.failure = not loaded and bridge.failure(candidate) or nil
    entry.ran = false
    install(entry)
end

-- A plugin that stops takes back everything it registered, so no destination, band, setting, subscription or surface of it is left behind.
-- Every step runs on its own, so one the host refuses never keeps the others, and the removal from the registry comes last because the host answers the earlier steps only for a registered plugin.
local function withdraw(entry)
    local id = entry.id
    lifecycle.withdraw(id)

    local steps = {
        function() events.forget(id) end,
        function() logs.forget(id) end,
        function() reader.forget(id) end,
        function() preferences.forget(id) end,
        function() http.forget(id) end,
        function() process.forget(id) end,
        function() sounds.forget(id) end,
        function() ui.unmountOwner(id) end,
        function() bridge.call("workpane_plugin_remove", { plugin = id }) end,
    }

    for _, step in ipairs(steps) do
        local succeeded, failure = pcall(step)

        if not succeeded then
            bridge.report("workpane", "plugins", "A plugin could not be withdrawn completely", { plugin = id, error = tostring(failure) })
        end
    end

    for index, candidate in ipairs(started) do
        if candidate == id then
            table.remove(started, index)
            break
        end
    end
end

-- A plugin acts only once the host holds its manifest, so what its definition did while it was loaded never outlives a refusal.
local function start(entry)
    if entry.ran then
        reread(entry)

        if entry.candidate == nil then
            entry.attempt = false
            tell({ id = entry.id, failure = entry.failure })
            return
        end
    end

    local definition = entry.candidate.definition
    local answered, enabled = pcall(function()
        return definition.enabled == nil or definition.enabled()
    end)

    if not answered then
        refuse(entry, enabled)
        return
    end

    if not enabled then
        entry.failure = nil
        entry.attempt = false
        enter(entry, "unavailable", "Its enabled function answered false.")
        return
    end

    enter(entry, "starting", "Its dependencies run.")
    local registered, refusal = pcall(bridge.call, "workpane_plugin_register", loader.manifest(entry.candidate))

    if not registered then
        refuse(entry, refusal)
        return
    end

    lifecycle.start(entry.id)
    started[#started + 1] = entry.id
    entry.ran = true

    -- The tables of a plugin are brought to the migrations it declares before its start, so its start already finds them.
    if definition.migrations ~= nil then
        local _, failure = bridge.request("workpane_database_migrate", { plugin = entry.id, migrations = definition.migrations }):await()

        if failure ~= nil then
            withdraw(entry)
            refuse(entry, failure)
            return
        end
    end

    if definition.start ~= nil then
        local succeeded, failure = pcall(definition.start)

        if not succeeded then
            withdraw(entry)
            refuse(entry, failure)
            return
        end
    end

    entry.failure = nil
    enter(entry, "running", "Its start finished.")

    -- The surfaces the shell asked for while the plugin was starting are built now that it runs.
    local requests = entry.requests or {}
    entry.requests = {}

    for _, request in ipairs(requests) do
        request(definition)
    end
end

-- Orders the loaded plugins so every dependency comes first, and answers the plugins of a cycle apart, since none of them can ever start.
local function ordered()
    local result = {}
    local cyclic = {}
    local state = {}

    local function visit(entry, path)
        if state[entry.id] == "done" then
            return
        end

        if state[entry.id] == "visiting" then
            for index = #path, 1, -1 do
                cyclic[path[index]] = true

                if path[index] == entry.id then
                    break
                end
            end

            return
        end

        state[entry.id] = "visiting"
        path[#path + 1] = entry.id

        for _, dependency in ipairs(entry.candidate.definition.dependencies or {}) do
            local required = entries[dependency]

            if required ~= nil and required.candidate ~= nil then
                visit(required, path)
            end
        end

        path[#path] = nil
        state[entry.id] = "done"
        result[#result + 1] = entry
    end

    for _, id in ipairs(discovered) do
        if entries[id].candidate ~= nil then
            visit(entries[id], {})
        end
    end

    return result, cyclic
end

-- The first dependency that is not installed, and the first one that is installed but does not run.
local function blocking(entry)
    for _, dependency in ipairs(entry.candidate.definition.dependencies or {}) do
        local required = entries[dependency]

        if required == nil then
            return dependency, nil
        end

        if required.state ~= "running" then
            return nil, dependency
        end
    end

    return nil, nil
end

-- Starts every plugin that may run and whose dependencies run, in dependency order, and says why each other one waits.
local function resolve()
    local order, cyclic = ordered()

    for _, entry in ipairs(order) do
        if entry.state ~= "running" and disabled[entry.id] then
            entry.failure = nil
            enter(entry, "disabled", "The reader turned it off.")
        elseif entry.state ~= "running" and entry.attempt then
            local missing, inactive = blocking(entry)

            if cyclic[entry.id] then
                refuse(entry, { code = "plugin_dependency_cycle", message = "Plugins depend on each other in a cycle", detail = entry.id })
            elseif missing ~= nil then
                refuse(entry, { code = "plugin_dependency_missing", message = "A plugin depends on a plugin that is not installed", detail = entry.id .. " -> " .. missing })
            elseif inactive ~= nil then
                entry.failure = bridge.failure({ code = "plugin_dependency_inactive", message = "A plugin waits for a plugin it depends on to run", detail = inactive })
                enter(entry, "waiting", "It waits for \"" .. inactive .. "\".")
            else
                start(entry)
            end
        end
    end
end

-- The running plugins that depend on a plugin, directly or through others, from the last started to the first, found in start order since a dependency always starts first.
local function dependents(id)
    local found = { [id] = true }
    local result = {}

    for index = 1, #started do
        local entry = entries[started[index]]

        for _, dependency in ipairs(entry.candidate.definition.dependencies or {}) do
            if found[dependency] and not found[entry.id] then
                found[entry.id] = true
            end
        end
    end

    for index = #started, 1, -1 do
        if found[started[index]] and started[index] ~= id then
            result[#result + 1] = started[index]
        end
    end

    return result
end

-- Runs the stop of a plugin for at most a grace, so a stop that waits forever never holds the switch of the reader or the closing of the product.
local function stop(entry, grace)
    local definition = entry.candidate.definition

    if definition.stop == nil then
        return
    end

    local finished, finish = async.deferred()
    local settled = false

    local function settle()
        if not settled then
            settled = true
            finish()
        end
    end

    bridge.spawn(function()
        local stopped, failure = pcall(definition.stop)

        if not stopped then
            bridge.report(entry.id, "shutdown", "A plugin failed while stopping", { error = tostring(failure) })
        end

        settle()
    end)

    bridge.spawn(function()
        async.sleep(grace):await()
        settle()
    end)

    finished:await()
end

-- One change of the plugins runs at a time, so a switch pressed twice or an erase during a start never meet half way, and the watchers hear when a change begins and when it ends.
local function exclusive(work)
    return bridge.future(function()
        while busy do
            local released, release = async.deferred()
            waiting[#waiting + 1] = release
            released:await()
        end

        busy = true
        announce()
        local succeeded, result = pcall(work)
        busy = false
        local nextOne = table.remove(waiting, 1)

        if nextOne ~= nil then
            nextOne()
        end

        announce()

        if not succeeded then
            error(result, 0)
        end

        return result
    end)
end

-- Discovers every plugin of the plugin folders, installs their catalogs and starts the ones that may run.
function plugins.boot(applicationInfo, switches)
    info = applicationInfo
    local found, refused = loader.discover(info.paths.plugins, info)

    for _, refusal in ipairs(refused) do
        tell(refusal)
    end

    for _, record in ipairs(found) do
        local entry = { id = record.id, directory = record.directory, bundled = record.bundled, candidate = record.candidate, failure = record.failure, attempt = true, requests = {} }
        local switched = switches[record.id]

        -- A plugin is off when the reader turned it off, or when it declares itself off by default and the reader never turned it on.
        if switched == false or (switched == nil and record.candidate ~= nil and record.candidate.definition.offByDefault == true) then
            disabled[record.id] = true
        end

        enter(entry, "discovered", "It was found in \"" .. record.directory .. "\".")
        entries[entry.id] = entry
        discovered[#discovered + 1] = entry.id
        install(entry)

        -- A plugin the reader turned off is only read again when it is turned on, so a failure to read it now is not told and it stays off.
        if entry.state == "refused" and disabled[entry.id] then
            entry.failure = nil
            enter(entry, "disabled", "The reader turned it off.")
        elseif entry.state == "refused" then
            tell({ id = entry.id, failure = entry.failure })
        end
    end

    resolve()
    announce()
end

-- Stops a plugin and every running plugin that depends on it, the dependents first, and keeps it disabled until the reader enables it again.
function plugins.disable(id)
    return exclusive(function()
        local entry = entries[id]

        if entry == nil then
            bridge.raise("plugin_unknown", "No plugin was discovered under that identifier", tostring(id))
        end

        disabled[id] = true

        if entry.state == "running" then
            enter(entry, "stopping", "The reader turned it off.")
        end

        for _, dependent in ipairs(dependents(id)) do
            enter(entries[dependent], "stopping", "A plugin it depends on is being turned off.")
            stop(entries[dependent], stopGraceMilliseconds)
            withdraw(entries[dependent])
            enter(entries[dependent], "waiting", "It waits for \"" .. id .. "\".")
        end

        if entry.state == "stopping" then
            stop(entry, stopGraceMilliseconds)
            withdraw(entry)
        end

        entry.failure = nil
        enter(entry, "disabled", "The reader turned it off.")
        resolve()
    end)
end

-- Reads a plugin from its folder again and starts it, then every plugin that waited for it.
function plugins.enable(id)
    return exclusive(function()
        local entry = entries[id]

        if entry == nil then
            bridge.raise("plugin_unknown", "No plugin was discovered under that identifier", tostring(id))
        end

        if entry.state == "running" then
            return
        end

        disabled[id] = nil
        entry.attempt = true
        reread(entry)

        if entry.state == "refused" and entry.candidate == nil then
            entry.attempt = false
            tell({ id = id, failure = entry.failure })
        else
            enter(entry, "waiting", "The reader turned it on.")
        end

        resolve()
    end)
end

-- Erases what a plugin that does not run keeps in the database: its tables, its schema version and its preferences.
function plugins.erase(id)
    return exclusive(function()
        local entry = entries[id]

        if entry ~= nil and entry.state == "running" then
            bridge.raise("plugin_running", "The data of a running plugin cannot be erased", id)
        end

        local _, failure = bridge.request("workpane_plugin_data_erase", { plugin = "workpane", target = id }):await()

        if failure ~= nil then
            error(failure, 0)
        end
    end)
end

-- Answers the plugins that keep data in the database, installed or not.
function plugins.stored()
    return bridge.future(function()
        local value, failure = bridge.request("workpane_plugin_data", { plugin = "workpane" }):await()

        if failure ~= nil then
            error(failure, 0)
        end

        return value.plugins
    end)
end

-- Answers the number of migrations every loaded plugin declares, which is the newest schema of its tables its code reads, whether or not it ever ran here.
function plugins.schemas()
    local schemas = {}

    for _, id in ipairs(discovered) do
        local candidate = entries[id].candidate

        if candidate ~= nil then
            schemas[id] = #(candidate.definition.migrations or {})
        end
    end

    return schemas
end

-- Answers every discovered plugin with its state, the reason it does not run and the plugins it depends on and that depend on it.
function plugins.list()
    local list = {}

    for _, id in ipairs(discovered) do
        local entry = entries[id]
        local definition = entry.candidate ~= nil and entry.candidate.definition or {}
        local dependencies = {}
        local dependentIds = {}

        for index, dependency in ipairs(definition.dependencies or {}) do
            dependencies[index] = dependency
        end

        for _, other in ipairs(discovered) do
            local candidate = entries[other].candidate

            for _, dependency in ipairs(candidate ~= nil and candidate.definition.dependencies or {}) do
                if dependency == id then
                    dependentIds[#dependentIds + 1] = other
                end
            end
        end

        local failure = entry.failure and { code = entry.failure.code, message = entry.failure.message, detail = entry.failure.detail } or nil
        list[#list + 1] = { id = id, titleKey = entry.catalog and definition.titleKey or nil, descriptionKey = entry.catalog and definition.descriptionKey or nil, directory = entry.directory, bundled = entry.bundled, state = entry.state, disabled = disabled[id] == true, failure = failure, dependencies = dependencies, dependents = dependentIds }
    end

    return list
end

-- The identifiers of the running plugins that depend on one, which a switch that disables it stops too.
function plugins.dependents(id)
    return dependents(id)
end

-- Runs a function after every change of the plugins, which is how the core settings keep the list of plugins current.
function plugins.watch(handler)
    watchers[#watchers + 1] = handler
end

-- Answers whether a change of the plugins is running, while which the switches of the reader wait.
function plugins.changing()
    return busy
end

-- Builds a surface of a plugin with its definition once it runs: at once when it runs, as soon as its start finishes when it is starting, and never when it does not run.
function plugins.serve(id, build)
    local entry = entries[id]

    if entry == nil then
        return
    end

    if entry.state == "running" then
        build(entry.candidate.definition)
        return
    end

    if entry.state == "starting" then
        entry.requests[#entry.requests + 1] = build
    end
end

-- Answers the definition of a running plugin, which is where its views, bands, sections and shortcuts are found.
function plugins.definition(id)
    local entry = entries[id]

    if entry == nil or entry.state ~= "running" then
        return nil
    end

    return entry.candidate.definition
end

-- Plugins stop in the reverse order they started once the change in progress ends, so a dependency outlives every plugin that relies on it and no stop runs beside a start or a stop of the same plugin.
-- Each stop runs for at most its share of the time the product gives its closing, so one slow stop never keeps the plugins started before it from stopping.
-- The plugins reading the log stop last, after a moment that hands them what the other stops wrote, so the log keeps the closing of every plugin.
function plugins.stopAll()
    return exclusive(function()
        local share = math.min(stopGraceMilliseconds, closingMilliseconds // math.max(1, #started))
        local readers = {}

        for index = #started, 1, -1 do
            local entry = entries[started[index]]

            if logs.reads(entry.id) then
                readers[#readers + 1] = entry
            else
                enter(entry, "stopping", "The product is closing.")
                stop(entry, share)
            end
        end

        async.sleep(handoverMilliseconds):await()

        for _, entry in ipairs(readers) do
            enter(entry, "stopping", "The product is closing.")
            stop(entry, share)
        end
    end)
end

return plugins
