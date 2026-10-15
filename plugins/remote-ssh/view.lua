local connections = include("connections")
local diagnostics = include("diagnostics")
local preferences = include("preferences")
local form = include("views/profile-form")
local terminals = include("views/terminals")
local ports = include("views/ports")

local ui = workpane.ui
local text = workpane.i18n.text
local translate = workpane.i18n.translate

local view = {}

local function remove(id)
    local profile = preferences.find(id)

    if profile == nil then
        return
    end

    local confirmed = workpane.await(workpane.dialogs.confirm({ title = translate("remote-ssh.dialog.remove"), message = translate("remote-ssh.dialog.remove-message", profile.name), confirmText = translate("remote-ssh.dialog.remove-action"), destructive = true }))

    if not confirmed then
        return
    end

    local closed = connections.disconnect(id):await()

    if closed.failure ~= nil then
        diagnostics.notify(closed.failure)
        return
    end

    local removed = preferences.remove(id)

    if not removed then
        workpane.notify.error(translate("remote-ssh.plugin.title"), translate("remote-ssh.error.remove"))
    end
end

function view.build()
    local pages = {}
    local editing = {}
    local removing = {}
    local current = "connections"
    local tabs = ui.tabs({ items = {}, current = current, closable = true, grow = 1, height = 0 })
    local empty = ui.emptyState({ text = text("remote-ssh.manager.empty"), icon = "terminal", grow = 1 })
    local operations = ui.column({ visible = false, spacing = 8 }, {})
    local grid = ui.table({ columns = {
        { id = "name", title = text("remote-ssh.field.name") },
        { id = "host", title = text("remote-ssh.field.host") },
        { id = "root", title = text("remote-ssh.field.root"), width = "stretch" },
        { id = "platform", title = text("remote-ssh.field.platform") },
        { id = "status", title = text("remote-ssh.field.status") },
    }, rows = {}, header = true, selection = "subtle", grow = 1 })

    local manager = ui.column({ padding = 16, spacing = 12, grow = 1, height = 0 }, {
        ui.row({ spacing = 8 }, {
            ui.sectionTitle({ text = text("remote-ssh.navigation.connections"), grow = 1 }),
            ui.button({ text = text("remote-ssh.dialog.new"), icon = "add", onClick = function()
                form.open()
            end }),
        }),
        operations,
        empty,
        grid,
    })

    local function refresh()
        local rows = {}
        local waiting = {}
        local items = { { id = "connections", text = text("remote-ssh.navigation.connections"), closable = false } }
        local children = { manager }
        local found = current == "connections"

        for index, profile in ipairs(preferences.list()) do
            local active = connections.active(profile.id)
            local state = connections.status(profile.id)
            local session = connections.current(profile.id)
            local actions = {
                { id = active and "disconnect" or "connect", icon = active and "stop" or "play", tooltip = text(active and "remote-ssh.actions.disconnect" or "remote-ssh.actions.connect") },
            }

            if state == "closing" or removing[profile.id] then
                actions = {}
            end

            if removing[profile.id] then
                waiting[#waiting + 1] = ui.row({ spacing = 8 }, {
                    ui.busyIndicator({ running = true, size = 16 }),
                    ui.label({ text = text("remote-ssh.operation.remove", profile.name), grow = 1 }),
                })
            end

            if active and state ~= "connected" then
                waiting[#waiting + 1] = ui.row({ spacing = 8 }, {
                    ui.busyIndicator({ running = true, size = 16 }),
                    ui.label({ text = text("remote-ssh.operation.connection", profile.name, translate("remote-ssh.state." .. state)), grow = 1 }),
                    ui.button({ text = text("remote-ssh.dialog.cancel"), enabled = state ~= "closing", onClick = function()
                        local outcome = connections.disconnect(profile.id):await()

                        if outcome.failure ~= nil then
                            diagnostics.notify(outcome.failure)
                        end
                    end }),
                })
            end

            if session ~= nil then
                actions[#actions + 1] = { id = "terminal", icon = "terminal", tooltip = text("remote-ssh.terminal.open") }
                actions[#actions + 1] = { id = "ports", icon = "network", tooltip = text("remote-ssh.ports.title") }

                if not editing[profile.id] and workpane.capabilities.available("workspace.remote.open") then
                    actions[#actions + 1] = { id = "editor", icon = "edit", tooltip = text("remote-ssh.actions.editor") }
                end
            end

            if not active and not removing[profile.id] then
                actions[#actions + 1] = { id = "edit", icon = "edit", tooltip = text("remote-ssh.dialog.edit") }
                actions[#actions + 1] = { id = "remove", icon = "close", tooltip = text("remote-ssh.dialog.remove"), destructive = true }
            end

            rows[index] = { id = profile.id, cells = { profile.name, profile.host, session ~= nil and session.identity.root or profile.root, session ~= nil and session.identity.platform or translate("remote-ssh.platform.automatic"), translate("remote-ssh.state." .. state) }, actions = actions }

            for _, prefix in ipairs({ "profile:", "ports:" }) do
                local id = prefix .. profile.id
                local page = pages[id]

                if page ~= nil and page.session ~= session then
                    if page.dispose ~= nil then
                        page.dispose()
                    end

                    pages[id] = nil
                    page = nil
                end

                if page ~= nil then
                    items[#items + 1] = { id = id, text = prefix == "ports:" and text("remote-ssh.ports.tab", profile.name) or profile.name, icon = prefix == "ports:" and "network" or "terminal" }
                    children[#children + 1] = page.root
                    found = found or current == id
                end
            end
        end

        if not found then
            current = "connections"
        end

        grid:set({ rows = rows, visible = #rows > 0 })
        operations:setChildren(waiting, { visible = #waiting > 0 })
        empty:set({ visible = #rows == 0 })
        tabs:setChildren(children, { items = items, current = current })
    end

    tabs:on("select", function(event)
        current = event.id
        local page = pages[event.id]

        if page ~= nil then
            page.focus()
        end
    end)

    tabs:on("close", function(event)
        local page = pages[event.id]

        if page == nil then
            return
        end

        pages[event.id] = nil

        if page.dispose ~= nil then
            page.dispose()
        end

        refresh()
    end)

    grid:on("action", function(event)
        if removing[event.id] then
            return
        end

        if event.action == "connect" then
            local pending, refused = connections.connect(event.id)

            if pending == nil then
                diagnostics.notify(refused)
                return
            end

            local outcome = pending:await()

            if outcome.failure ~= nil and outcome.failure.code ~= "ssh_connection_closed" then
                diagnostics.notify(outcome.failure)
            end
        elseif event.action == "disconnect" then
            local outcome = connections.disconnect(event.id):await()

            if outcome.failure ~= nil then
                diagnostics.notify(outcome.failure)
            end
        elseif event.action == "edit" and not connections.active(event.id) then
            form.open(event.id)
        elseif event.action == "remove" and not connections.active(event.id) then
            removing[event.id] = true
            refresh()
            local accepted, problem = pcall(remove, event.id)
            removing[event.id] = nil
            refresh()

            if not accepted then
                diagnostics.notify(type(problem) == "table" and problem or { code = "ssh_cleanup_failed" })
            end
        elseif event.action == "editor" then
            local session = connections.current(event.id)

            if session ~= nil and not editing[event.id] then
                editing[event.id] = true
                refresh()
                local _, problem = workpane.capabilities.request("workspace.remote.open", { profile = event.id, session = session.id }):await()
                editing[event.id] = nil
                refresh()

                if problem ~= nil and problem.code ~= "code_editor_open_cancelled" then
                    workpane.notify.error(translate("remote-ssh.plugin.title"), translate("remote-ssh.error.editor"))
                end
            end
        elseif event.action == "terminal" or event.action == "ports" then
            local session = connections.current(event.id)

            if session ~= nil then
                local id = (event.action == "ports" and "ports:" or "profile:") .. event.id

                if pages[id] == nil then
                    local builder = event.action == "ports" and ports or terminals
                    local root, focus, dispose = builder.build(session)
                    pages[id] = { session = session, root = root, focus = focus, dispose = dispose }
                end

                current = id
                refresh()
                pages[id].focus()
            end
        end
    end)

    grid:on("activate", function(event)
        if not connections.active(event.id) and not removing[event.id] then
            form.open(event.id)
        end
    end)

    preferences.watch(refresh)
    connections.watch(refresh)
    workpane.app.watch(refresh)
    workpane.capabilities.watch("workspace.remote.open", refresh)
    refresh()

    return tabs
end

return view
