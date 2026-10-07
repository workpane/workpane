-- Builds the API one plugin sees, with its identity bound inside every call so no plugin can speak for another one.
local bridge = require("workpane.bridge")
local discovered = require("workpane.directory")
local events = require("workpane.events")
local http = require("workpane.http")
local lifecycle = require("workpane.lifecycle")
local logs = require("workpane.logs")
local paths = require("workpane.paths")
local preferences = require("workpane.preferences")
local process = require("workpane.process")
local reader = require("workpane.reader")
local sounds = require("workpane.sounds")
local task = require("workpane.task")
local ui = require("workpane.ui")

local api = {}

-- The level of the SDK a plugin declares as its `sdk`, raised whenever a change of the API would break a plugin written for the previous one.
api.level = 1

local nextDialog = 0

-- A number made by the number function and a sentence made by the text function stay as they are, so the shell writes them in the language being read, and every other value is written out as text.
local function textArguments(...)
    local values = table.pack(...)
    local arguments = {}

    for index = 1, values.n do
        local value = values[index]
        local composed = type(value) == "table" and (type(value.number) == "number" or type(value.key) == "string")
        arguments[index] = composed and value or tostring(value)
    end

    return arguments
end

-- A number argument of a sentence, written with the separators of the current language and rounded to its decimals.
function api.number(value, decimals)
    if type(value) ~= "number" or value ~= value or value == math.huge or value == -math.huge then
        error(bridge.failure({ code = "translation_argument_invalid", message = "A number argument is not a finite number", detail = tostring(value) }), 2)
    end

    local places = decimals or 0

    if math.type(places) ~= "integer" or places < 0 or places > 6 then
        error(bridge.failure({ code = "translation_argument_invalid", message = "A number argument declares decimals outside zero to six", detail = tostring(decimals) }), 2)
    end

    return { number = value, decimals = places }
end

-- A translated text travels as its key, so the shell resolves it again whenever the reader changes the language.
function api.text(key, ...)
    return { key = key, args = textArguments(...) }
end

local function interfaceFor(owner)
    local interface = {}

    for name, builder in pairs(ui) do
        if type(builder) == "function" and name ~= "mount" and name ~= "unmount" and name ~= "unmountOwner" then
            interface[name] = builder
        end
    end

    -- Answers how wide a text is in points in the face and the size a canvas draws it with, so a drawing is laid out around its words.
    function interface.textWidth(text, options)
        local chosen = options or {}
        return bridge.call("workpane_ui_measure", { plugin = owner, text = text, size = chosen.size, face = chosen.face })
    end

    return interface
end

-- The window around the views of a plugin: moving to one of its destinations and changing the height of one of its bands.
local function shellFor(owner)
    return {
        navigate = function(item)
            bridge.call("workpane_navigate", { plugin = owner, item = item })
        end,
        resizeBand = function(item, height)
            bridge.call("workpane_band_resize", { plugin = owner, item = item, height = height })
        end,
    }
end

local function dialogsFor(owner)
    local dialogs = {}

    local function open(options)
        options.plugin = owner

        return bridge.request("workpane_dialog_open", options)
    end

    -- Answers true only when the reader pressed the confirming button, and false for cancel, Escape and a closed product.
    function dialogs.confirm(options)
        return bridge.future(function()
            local answer = task.await(open({ kind = "confirm", title = options.title, message = options.message, detail = options.detail, confirmText = options.confirmText, cancelText = options.cancelText, destructive = options.destructive == true }))
            return answer.button == "confirm"
        end)
    end

    function dialogs.alert(options)
        return bridge.future(function()
            task.await(open({ kind = "alert", title = options.title, message = options.message, detail = options.detail, confirmText = options.confirmText }))
        end)
    end

    -- Answers the text the reader confirmed, or nil when the prompt was cancelled.
    function dialogs.prompt(options)
        return bridge.future(function()
            local answer = task.await(open({ kind = "prompt", title = options.title, message = options.message, value = options.value, placeholder = options.placeholder, confirmText = options.confirmText, cancelText = options.cancelText }))
            return answer.button == "confirm" and answer.value or nil
        end)
    end

    -- Mounts the content as a surface of its own for as long as the dialog is open and answers the identifier of the button that closed it.
    function dialogs.custom(options)
        nextDialog = nextDialog + 1
        local surface = "dialog:" .. owner .. ":" .. nextDialog
        ui.mount(owner, surface, options.content)

        local dialog = bridge.future(function()
            local answered, answer = pcall(task.await, open({ kind = "custom", title = options.title, message = options.message, surface = surface, buttons = options.buttons, width = options.width }))
            ui.unmount(owner, surface)

            if not answered then
                error(answer, 0)
            end

            return answer.button
        end)

        -- Closes the dialog as if the reader pressed the named button, which lets content inside it finish the dialog it belongs to.
        function dialog:close(button)
            bridge.call("workpane_dialog_close", { plugin = owner, surface = surface, button = button })
        end

        -- Replaces the buttons of the open dialog, so they follow the state of what it shows.
        function dialog:setButtons(buttons)
            bridge.call("workpane_dialog_buttons", { plugin = owner, surface = surface, buttons = buttons })
        end

        -- A button declared with closes set to false reports its press here, so the plugin checks its form and closes the dialog itself.
        if options.onButton ~= nil then
            options.content:on("dialog-button", function(event)
                options.onButton(event.button, dialog)
            end)
        end

        return dialog
    end

    function dialogs.openFile(options)
        return bridge.future(function()
            return task.await(bridge.request("workpane_native_open", { plugin = owner, title = options.title, initial = options.initial, filters = options.filters, multiple = options.multiple == true })).paths
        end)
    end

    function dialogs.selectFolder(options)
        return bridge.future(function()
            return task.await(bridge.request("workpane_native_folder", { plugin = owner, title = options.title, initial = options.initial })).paths[1]
        end)
    end

    function dialogs.saveFile(options)
        return bridge.future(function()
            return task.await(bridge.request("workpane_native_save", { plugin = owner, title = options.title, initial = options.initial, filters = options.filters })).paths[1]
        end)
    end

    function dialogs.message(options)
        return bridge.future(function()
            return task.await(bridge.request("workpane_native_message", { plugin = owner, title = options.title, message = options.message, kind = options.kind, buttons = options.buttons })).button
        end)
    end

    return dialogs
end

local function notifyFor(owner)
    local notify = {}

    for _, severity in ipairs({ "information", "success", "warning", "error" }) do
        notify[severity] = function(title, message)
            bridge.call("workpane_notify", { plugin = owner, title = title, message = message, severity = severity })
        end
    end

    -- A notification of the operating system reaches the reader even while the product window is in the background.
    function notify.system(title, message, kind)
        bridge.call("workpane_native_notify", { plugin = owner, title = title, message = message, kind = kind })
    end

    return notify
end

local function logFor(owner)
    local log = {}

    for _, level in ipairs({ "debug", "info", "warning", "error" }) do
        log[level] = function(category, message, details)
            if logs.delivering() then
                bridge.raise("log_write_in_delivery", "A log subscriber cannot write to the log it reads", tostring(category))
            end

            bridge.call("workpane_log", { plugin = owner, level = level, category = category, message = message, details = details or {} })
        end
    end

    -- Receives every entry of the centralized log, which is how a plugin keeps or shows the history of the whole product.
    function log.subscribe(handler)
        logs.subscribe(owner, handler)
    end

    return log
end

-- A Lua list cannot hold nil, so a missing value of a statement is written as the null marker of the plugin, which the host binds as SQL NULL.
local function databaseFor(owner)
    return {
        null = { null = true },
        query = function(sql, bindings)
            return bridge.request("workpane_database_query", { plugin = owner, sql = sql, bindings = bindings or {} })
        end,
        run = function(sql, bindings)
            return bridge.request("workpane_database_run", { plugin = owner, sql = sql, bindings = bindings or {} })
        end,
        transaction = function(statements)
            return bridge.request("workpane_database_transaction", { plugin = owner, statements = statements })
        end,
        -- Stored rows that break the rules of the plugin refuse its start with the one failure the product names to the reader with the place of the data.
        invalid = function(detail)
            error(bridge.failure({ code = "database_rows_invalid", message = "The stored rows of the plugin break its rules", detail = detail or "" }), 0)
        end,
    }
end

-- Every plugin receives its own copies of the lists of the product, so no plugin changes what another one reads.
local function copiedList(list)
    local copy = {}

    for key, item in pairs(list) do
        copy[key] = type(item) == "table" and copiedList(item) or item
    end

    return copy
end

-- The whole API of one plugin, whose every call carries the identity it was built for.
function api.create(owner, directory, info)
    local workpane = {}

    workpane.plugin = { id = owner, directory = directory }
    workpane.app = { version = info.version, sdk = api.level, debug = info.debug, platform = info.platform, architecture = info.architecture, processId = info.processId, dataDirectory = info.paths.data, languages = copiedList(info.languages), themes = copiedList(info.themes), icons = copiedList(info.icons), colors = copiedList(info.colors) }

    function workpane.app.language()
        return bridge.call("workpane_app_info", {}).language
    end

    function workpane.app.theme()
        return bridge.call("workpane_app_info", {}).theme
    end

    function workpane.app.quit()
        bridge.call("workpane_quit", {})
    end

    -- Answers every plugin the product discovered with its catalog keys and its state, so a plugin can name the others to the reader or to an agent.
    function workpane.app.plugins()
        return discovered.list()
    end

    -- Follows the language and the theme the reader chooses, receiving both after each change.
    function workpane.app.watch(handler)
        return reader.watch(owner, handler)
    end

    workpane.ui = interfaceFor(owner)
    workpane.shell = shellFor(owner)
    workpane.i18n = {
        text = api.text,
        number = api.number,
        translate = function(key, ...)
            return bridge.call("workpane_translate", { key = key, args = textArguments(...) })
        end,
    }
    workpane.preferences = {
        define = function(schema, options)
            return preferences.define(owner, schema, options)
        end,
    }
    workpane.database = databaseFor(owner)
    workpane.events = {
        publish = function(topic, payload)
            events.publish(owner, topic, payload or {})
        end,
        subscribe = function(topic, handler)
            events.subscribe(owner, topic, handler)
        end,
    }
    workpane.capabilities = {
        provide = function(name, handler, contract)
            events.provide(owner, name, handler, contract)
        end,
        list = events.catalog,
        request = function(name, payload)
            return events.request(owner, name, payload or {})
        end,
        available = events.available,
        watch = function(name, handler)
            return events.watch(owner, name, handler)
        end,
    }
    workpane.audio = {
        play = function(path, options)
            return sounds.play(owner, path, options)
        end,
        stop = function(sound)
            sounds.stop(owner, sound)
        end,
        stopAll = function()
            sounds.stopAll(owner)
        end,
    }
    workpane.dialogs = dialogsFor(owner)
    workpane.notify = notifyFor(owner)
    workpane.log = logFor(owner)
    workpane.system = {
        openUrl = function(url)
            return bridge.request("workpane_open_url", { plugin = owner, url = url })
        end,
        revealPath = function(path)
            return bridge.request("workpane_reveal_path", { plugin = owner, path = path })
        end,
        copy = function(text)
            bridge.call("workpane_clipboard_write", { plugin = owner, text = text })
        end,
        paste = function()
            return bridge.call("workpane_clipboard_read", { plugin = owner }).text
        end,
        information = function()
            return bridge.request("workpane_system_information", { plugin = owner })
        end,
        monospaceFonts = function()
            return bridge.future(function()
                return task.await(bridge.request("workpane_system_monospace_fonts", { plugin = owner })).families
            end)
        end,
        home = function()
            return bridge.call("workpane_system_home", { plugin = owner }).path
        end,
        shell = function()
            local shell = bridge.call("workpane_system_shell", { plugin = owner })
            return { name = shell.name, path = shell.path }
        end,
    }
    workpane.files = {
        list = function(path)
            return bridge.future(function()
                return task.await(bridge.request("workpane_files_list", { plugin = owner, path = path })).entries
            end)
        end,
        walk = function(root, options)
            local chosen = options or {}
            return bridge.request("workpane_files_walk", { plugin = owner, root = root, maximum = chosen.maximum, skip = chosen.skip or {} })
        end,
        search = function(root, options)
            local chosen = options or {}
            return bridge.request("workpane_files_search", { plugin = owner, root = root, text = chosen.text, maximumMatches = chosen.maximumMatches, maximumFileBytes = chosen.maximumFileBytes, skip = chosen.skip or {} })
        end,
        access = function(path)
            return bridge.request("workpane_files_access", { plugin = owner, path = path })
        end,
        canonical = function(path)
            return bridge.future(function()
                return task.await(bridge.request("workpane_files_canonical", { plugin = owner, path = path })).path
            end)
        end,
        replace = function(source, destination)
            return bridge.request("workpane_files_replace", { plugin = owner, source = source, destination = destination })
        end,
        absolute = function(path)
            return paths.absolute(info.platform, path)
        end,
        uri = function(path)
            if not paths.absolute(info.platform, path) then
                error(bridge.failure({ code = "files_path_invalid", message = "A file address is written from an absolute path", detail = tostring(path) }), 2)
            end

            return paths.uri(path)
        end,
        path = function(uri)
            return paths.path(info.platform, uri)
        end,
    }
    workpane.process = {
        start = function(options)
            return process.start(owner, options)
        end,
        write = function(identity, text)
            process.write(owner, identity, text)
        end,
        stop = function(identity)
            process.stop(owner, identity)
        end,
        find = function(name, directories)
            return process.find(owner, name, directories)
        end,
    }
    workpane.http = {
        serve = function(options)
            return http.serve(owner, options)
        end,
        stop = function(server)
            http.stop(owner, server)
        end,
    }
    workpane.time = {
        now = function()
            return bridge.call("workpane_time_now", {})
        end,
        localPresentation = function(timestamp)
            return bridge.call("workpane_time_local", { timestamp = timestamp })
        end,
        zone = function()
            return bridge.call("workpane_time_zone", {})
        end,
        offset = function(zone, seconds)
            return bridge.call("workpane_time_offset", { zone = zone, seconds = seconds })
        end,
    }
    workpane.task = function(work, ...)
        lifecycle.check(owner)

        if type(work) ~= "function" then
            bridge.raise("task_work_invalid", "A task runs a function", owner)
        end

        task.run(owner, "task", work, ...)
    end

    workpane.await = task.await
    workpane.isFailure = bridge.isFailure

    return workpane
end

return api
