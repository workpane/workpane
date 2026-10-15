local diagnostics = include("diagnostics")

local ui = workpane.ui
local text = workpane.i18n.text

local terminals = {}

function terminals.build(session)
    local disposed = false
    local entries = {}
    local current = ""
    local nextId = 0
    local tabs = ui.tabs({ items = {}, closable = true, grow = 1, height = 0 })
    local empty = ui.emptyState({ text = text("remote-ssh.terminal.empty"), icon = "terminal", grow = 1 })
    local restart = ui.button({ icon = "refresh", variant = "toolbar", tooltip = text("remote-ssh.terminal.restart"), enabled = false })

    local function render()
        if disposed then
            return
        end

        local items = {}
        local pages = {}

        for index, entry in ipairs(entries) do
            items[index] = { id = entry.id, text = text("remote-ssh.terminal.title", tostring(entry.number)), icon = "terminal" }
            pages[index] = entry.root
        end

        tabs:setChildren(pages, { items = items, current = current, visible = #items > 0 })
        empty:set({ visible = #items == 0 })
        restart:set({ enabled = #items > 0 })
    end

    local function add()
        if disposed then
            return
        end

        local loading = ui.row({ padding = 12, spacing = 8 }, {
            ui.busyIndicator({ running = true, size = 16 }),
            ui.label({ text = text("remote-ssh.terminal.starting"), grow = 1 }),
        })
        local owner, failure = session:openTerminal(diagnostics.notify, function(waiting)
            if not disposed then
                loading:set({ visible = waiting })
            end
        end)

        if owner == nil then
            diagnostics.notify(failure)
            return
        end

        nextId = nextId + 1
        local id = tostring(nextId)
        entries[#entries + 1] = { id = id, number = nextId, owner = owner, root = ui.column({ grow = 1, height = 0 }, { loading, owner.node }) }
        current = id
        render()
        owner.node:command("focus")
    end

    local function focus()
        if disposed then
            return
        end

        for _, entry in ipairs(entries) do
            if entry.id == current then
                entry.owner.node:command("focus")
                return
            end
        end
    end

    tabs:on("select", function(event)
        current = event.id
        focus()
    end)

    tabs:on("close", function(event)
        for index, entry in ipairs(entries) do
            if entry.id == event.id then
                entry.owner:close()
                table.remove(entries, index)

                if current == event.id then
                    local nextEntry = entries[math.min(index, #entries)]
                    current = nextEntry ~= nil and nextEntry.id or ""
                end

                render()
                return
            end
        end
    end)

    restart:on("click", function()
        for _, entry in ipairs(entries) do
            if entry.id == current then
                entry.owner.node:command("restart")
                entry.owner.node:command("focus")
                return
            end
        end
    end)

    local root = ui.column({ grow = 1, height = 0 }, {
        ui.row({ spacing = 8, padding = { 8, 12 }, background = "panel" }, {
            ui.label({ text = session.identity.root, wrap = false, grow = 1 }),
            restart,
            ui.button({ text = text("remote-ssh.terminal.new"), icon = "add", variant = "toolbar", onClick = add }),
        }),
        empty,
        tabs,
    })
    add()

    return root, focus, function()
        disposed = true

        for _, entry in ipairs(entries) do
            entry.owner:close()
        end

        entries = {}
    end
end

return terminals
