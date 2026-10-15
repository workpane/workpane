local diagnostics = include("diagnostics")

local ui = workpane.ui
local text = workpane.i18n.text
local translate = workpane.i18n.translate

local ports = {}

function ports.build(session)
    local active = true
    local remote = ui.numberField({ value = 3000, minimum = 1, maximum = 65535, decimals = 0, step = 1, width = 140 })
    local localPort = ui.numberField({ value = 0, minimum = 0, maximum = 65535, decimals = 0, step = 1, width = 140 })
    local empty = ui.emptyState({ text = text("remote-ssh.ports.empty"), icon = "network", grow = 1 })
    local grid = ui.table({ columns = {
        { id = "remote", title = text("remote-ssh.ports.remote") },
        { id = "local", title = text("remote-ssh.ports.local"), width = "stretch" },
        { id = "status", title = text("remote-ssh.field.status") },
    }, rows = {}, header = true, grow = 1 })
    local busy = false
    local closing = {}
    local status = ui.row({ spacing = 8, visible = false }, {
        ui.busyIndicator({ running = true, size = 16 }),
        ui.label({ text = text("remote-ssh.ports.working"), grow = 1 }),
    })
    local create = ui.button({ text = text("remote-ssh.ports.add"), icon = "add" })

    local function refresh()
        if not active then
            return
        end

        local rows = {}
        local sorted = {}

        for _, entry in pairs(session.forwards) do
            sorted[#sorted + 1] = entry
        end

        table.sort(sorted, function(left, right)
            return left.localPort < right.localPort
        end)

        for index, entry in ipairs(sorted) do
            local actions = closing[entry.id] and {} or { { id = "close", icon = "close", tooltip = text("remote-ssh.ports.close") } }

            if not closing[entry.id] and workpane.capabilities.available("workspace.page.open") then
                actions[#actions + 1] = { id = "browser", icon = "external-link", tooltip = text("remote-ssh.ports.browser") }
            end

            rows[index] = { id = entry.id, cells = { "127.0.0.1:" .. tostring(entry.remotePort), "127.0.0.1:" .. tostring(entry.localPort), translate(closing[entry.id] and "remote-ssh.ports.closing" or entry.problem ~= nil and "remote-ssh.ports.failed" or "remote-ssh.ports.active") }, actions = actions }
        end

        grid:set({ rows = rows, visible = #rows > 0 })
        empty:set({ visible = #rows == 0 })
        create:set({ enabled = not busy and not session.closed })
        remote:set({ enabled = not busy and not session.closed })
        localPort:set({ enabled = not busy and not session.closed })
        status:set({ visible = busy or next(closing) ~= nil })
    end

    create:on("click", function()
        if busy or not active then
            return
        end

        busy = true
        refresh()
        local accepted, _, problem = pcall(session.openForward, session, math.floor(remote:get("value")), math.floor(localPort:get("value")))
        busy = false

        if active and (not accepted or problem ~= nil) then
            diagnostics.notify(problem or { code = "ssh_forward_failed" })
        end

        refresh()
    end)

    grid:on("action", function(event)
        local entry = session.forwards[event.id]

        if entry == nil or closing[event.id] or not active then
            return
        end

        if event.action == "close" then
            closing[event.id] = true
            refresh()
            local accepted = pcall(session.closeForward, session, event.id)
            closing[event.id] = nil

            if active and not accepted then
                diagnostics.notify({ code = "ssh_forward_failed" })
            end
        elseif event.action == "browser" then
            local _, problem = workpane.capabilities.request("workspace.page.open", { url = "http://127.0.0.1:" .. tostring(entry.localPort) .. "/" }):await()

            if problem ~= nil then
                workpane.notify.error(translate("remote-ssh.plugin.title"), translate("remote-ssh.ports.browser-failed"))
            end
        end

        refresh()
    end)

    local unwatchConnection = include("connections").watch(refresh)
    local unwatchApp = workpane.app.watch(refresh)
    local unwatchBrowser = workpane.capabilities.watch("workspace.page.open", refresh)
    refresh()
    local root = ui.column({ padding = 16, spacing = 12, grow = 1, height = 0 }, {
        ui.sectionTitle({ text = text("remote-ssh.ports.title") }),
        ui.row({ spacing = 12 }, {
            ui.formField({ label = text("remote-ssh.ports.remote") }, remote),
            ui.formField({ label = text("remote-ssh.ports.local") }, localPort),
            ui.formField({}, create),
        }),
        ui.label({ text = text("remote-ssh.ports.description"), style = "muted", wrap = true }),
        status,
        empty,
        grid,
    })

    return root, function() end, function()
        active = false
        unwatchConnection()
        unwatchApp()
        unwatchBrowser()
    end
end

return ports
