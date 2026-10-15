local bridge = require("workpane.bridge")
local task = require("workpane.task")
local ui = require("workpane.ui")
local paths = require("workpane.paths")

local picker = {}
local maximumEntries = 2000

local function text(key)
    return { key = key, args = {} }
end

local function translate(key)
    return bridge.call("workpane_translate", { key = key, args = {} })
end

function picker.open(owner, options, dialogs)
    local backend = options.backend

    if type(backend) ~= "table" or type(backend.identity) ~= "table" or type(backend.identity.root) ~= "string" or type(backend.available) ~= "boolean" or type(backend.canonical) ~= "function" or type(backend.list) ~= "function" or type(backend.stat) ~= "function" then
        bridge.raise("ui_folder_backend_invalid", "A folder selector requires a workspace identity and asynchronous filesystem functions", "")
    end

    local root = backend.identity.root
    local current = root
    local busy = false
    local live = true
    local dialog
    local navigation
    local selected
    local address = ui.textField({ value = options.initial ~= nil and options.initial ~= "" and options.initial or current, grow = 1, monospace = true })
    local entries = ui.list({ items = {} })
    local loading = ui.row({ spacing = 8, visible = false }, {
        ui.busyIndicator({ running = true, size = 16 }),
        ui.label({ text = text("workpane.window.loading") }),
    })
    local problem = ui.alert({ text = "", visible = false })
    local up = ui.button({ icon = "back", variant = "icon", tooltip = text("workpane.paths.parent") })

    local function buttons()
        return {
            { id = "cancel", text = translate("workpane.actions.cancel") },
            { id = "select", text = translate("workpane.paths.select-folder"), closes = false, enabled = not busy and selected ~= nil and backend.available },
        }
    end

    local function navigate(path)
        if busy or not live then
            return
        end

        busy = true
        loading:set({ visible = true })
        selected = nil
        dialog:setButtons(buttons())
        entries:set({ enabled = false })
        navigation:set({ enabled = false })
        local accepted, listing, canonical = pcall(function()
            local canonical = task.await(backend.canonical(paths.normalizeRoot(backend.platform, path)))
            local info = task.await(backend.stat(canonical))

            if info == nil or not info.isDir then
                bridge.raise("ui_folder_unavailable", "The selected path is not an available folder", "")
            end

            return task.await(backend.list(canonical, { maximum = maximumEntries })), canonical
        end)

        if not live then
            return
        end

        busy = false
        loading:set({ visible = false })
        entries:set({ enabled = backend.available })
        navigation:set({ enabled = backend.available })

        if not accepted then
            problem:set({ text = text("workpane.paths.folder-error"), tone = "danger", visible = true })
            dialog:setButtons(buttons())
            return
        end

        current = canonical
        selected = canonical
        address:set({ value = canonical })
        local items = {}

        for _, entry in ipairs(listing.entries) do
            if entry.kind == "directory" then
                local path = current:gsub("/+$", "") .. "/" .. entry.name
                items[#items + 1] = { id = path, text = entry.name, icon = "folder" }
            end
        end

        table.sort(items, function(first, second) return first.text:lower() < second.text:lower() end)
        entries:set({ items = items, selected = "" })
        up:set({ enabled = current ~= root })
        problem:set({ text = text("workpane.paths.incomplete"), tone = "warning", visible = not listing.complete })
        dialog:setButtons(buttons())
    end

    entries:on("select", function(event)
        selected = event.id ~= "" and event.id or current
        dialog:setButtons(buttons())
    end)

    entries:on("activate", function(event) navigate(event.id) end)
    address:on("submit", function(event) navigate(event.value) end)
    up:on("click", function()
        local parent = current:gsub("/+$", ""):match("^(.*)/[^/]+$")

        if parent ~= nil then
            navigate(parent == "" and "/" or parent:match("^%a:$") ~= nil and parent .. "/" or parent)
        end
    end)

    navigation = ui.row({ spacing = 8 }, { up, address, ui.button({ icon = "home", variant = "icon", tooltip = text("workpane.paths.root"), onClick = function() navigate(root) end }) })
    dialog = dialogs.custom({ title = options.title, width = 680, content = ui.column({ spacing = 8, height = 380 }, {
        navigation,
        loading,
        ui.scroll({ grow = 1 }, entries),
        problem,
    }), buttons = buttons(), onButton = function(action)
        if action ~= "select" or busy or selected == nil then
            return
        end

        busy = true
        loading:set({ visible = true })
        dialog:setButtons(buttons())
        entries:set({ enabled = false })
        navigation:set({ enabled = false })
        local accepted, canonical = pcall(function()
            local path = task.await(backend.canonical(selected))
            local info = task.await(backend.stat(path))

            if info == nil or not info.isDir then
                bridge.raise("ui_folder_unavailable", "The selected path is not an available folder", "")
            end

            return path
        end)

        if not live then
            return
        end

        busy = false
        loading:set({ visible = false })

        if not accepted then
            problem:set({ text = text("workpane.paths.folder-error"), tone = "danger", visible = true })
            entries:set({ enabled = backend.available })
            navigation:set({ enabled = backend.available })
            dialog:setButtons(buttons())
            return
        end

        selected = canonical
        dialog:close("select")
    end })

    local disconnected = backend.disconnected

    local function disconnect()
        if live then
            dialog:close("cancel")
        end

        if disconnected ~= nil then
            disconnected()
        end
    end

    backend.disconnected = disconnect
    task.run(owner, "interface", navigate, address:get("value"))
    local accepted, result = pcall(task.await, dialog)
    live = false

    if backend.disconnected == disconnect then
        backend.disconnected = disconnected
    end

    if not accepted then
        error(result, 0)
    end

    return result == "select" and selected or nil
end

return picker
