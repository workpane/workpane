-- Starts the product side of Lua: the core settings, every plugin in dependency order, and the answers to what the shell asks for.
local application = require("workpane.application")
local bridge = require("workpane.bridge")
local lifecycle = require("workpane.lifecycle")
local plugins = require("workpane.plugins")
local task = require("workpane.task")
local ui = require("workpane.ui")

local info = bridge.call("workpane_app_info", {})

local function findView(definition, item)
    for _, navigation in ipairs(definition.navigation or {}) do
        if navigation.id == item then
            return navigation.view
        end
    end

    return nil
end

local function findBand(definition, item)
    for _, band in ipairs(definition.bands or {}) do
        if band.id == item then
            return band.view
        end
    end

    return nil
end

local function findSection(definition, group, section)
    for _, declared in ipairs(definition.settings or {}) do
        if declared.id == group then
            for _, candidate in ipairs(declared.sections or {}) do
                if candidate.id == section then
                    return candidate.view
                end
            end
        end
    end

    return nil
end

local function findShortcut(definition, item, id)
    for _, navigation in ipairs(definition.navigation or {}) do
        if navigation.id == item then
            for _, shortcut in ipairs(navigation.shortcuts or {}) do
                if shortcut.id == id then
                    return shortcut.action
                end
            end
        end
    end

    return nil
end

-- A surface is built by the function its owner declared, and one that fails is replaced by the failure surface of the shell.
local function build(owner, surface, factory, context)
    task.run(owner, "interface", function()
        local built, failure = pcall(function()
            if factory == nil then
                bridge.raise("ui_builder_missing", "The plugin declares no builder for the surface", surface)
            end

            ui.mount(owner, surface, factory(context))
        end)

        if not built then
            bridge.report(owner, "interface", "A surface could not be built", { surface = surface, error = tostring(failure) })
            bridge.call("workpane_ui_failed", { plugin = owner, surface = surface })
        end
    end)
end

-- A surface asked for while its plugin is still starting is built once the start finishes, which is how a preloaded view of a plugin turned on again comes back.
bridge.on("workpane.view.open", function(request)
    plugins.serve(request.plugin, function(definition)
        build(request.plugin, request.surface, findView(definition, request.item), { item = request.item, surface = request.surface })
    end)
end)

bridge.on("workpane.band.open", function(request)
    plugins.serve(request.plugin, function(definition)
        build(request.plugin, request.surface, findBand(definition, request.item), { item = request.item, surface = request.surface })
    end)
end)

bridge.on("workpane.settings.open", function(request)
    if request.plugin == "workpane" then
        task.run("workpane", "interface", function()
            local built, failure = pcall(application.section, request.group, request.section, request.surface)

            if not built then
                bridge.report("workpane", "interface", "A settings section could not be built", { surface = request.surface, error = tostring(failure) })
                bridge.call("workpane_ui_failed", { plugin = "workpane", surface = request.surface })
            end
        end)

        return
    end

    plugins.serve(request.plugin, function(definition)
        build(request.plugin, request.surface, findSection(definition, request.group, request.section), { group = request.group, section = request.section, surface = request.surface })
    end)
end)

-- A shortcut runs the action its view declared, in a task of the plugin that owns the view.
bridge.on("workpane.shortcut", function(request)
    local definition = plugins.definition(request.plugin)
    local action = definition and findShortcut(definition, request.item, request.shortcut)

    if action ~= nil then
        task.run(request.plugin, "shortcuts", action)
    end
end)

bridge.on("workpane.shutdown", function()
    task.run("workpane", "shutdown", function()
        plugins.stopAll():await()
        bridge.call("workpane_stopped", {})
    end)
end)

-- The shell leaves its loading state whatever happened here, because a product that never finishes opening explains nothing.
task.run("workpane", "bootstrap", function()
    local booted, failure = pcall(function()
        lifecycle.start("workpane")
        application.register(info, info.paths.resources)
        plugins.boot(info, application.pluginSwitches())
    end)

    bridge.call("workpane_ready", {})

    if not booted then
        bridge.report("workpane", "bootstrap", "The plugins could not be started", { error = tostring(failure) })
    end
end)
