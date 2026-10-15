-- The Terminal destination: the workspace tabs and their actions, the terminals of the selected tab in its layout, the shelf and the status bar.
-- Every terminal stays mounted inside the board for as long as it exists, so a layout, a tab or the shelf only moves it and never restarts its shell.
local layouts = include("layouts")
local preferences = include("preferences")
local workspace = include("workspace")

local ui = workpane.ui
local text = workpane.i18n.text
local number = workpane.i18n.number
local translate = workpane.i18n.translate

local view = {}

local sessionKind = "terminal.session"
local editorCapability = "workspace.folder.open"
local serverCapability = "workspace.folder.serve"
local browserCapability = "workspace.page.open"
local swatchWidth = 48
local swatchSpacing = 3
local blinkIntervals = { slow = 800, normal = 530, fast = 300 }

local focusTooltip = text("terminal.actions.focus")
local restoreTooltip = text("terminal.actions.restore-layout")

local panes = {}
local swatches = {}
local controls
local home
local shape = ""
local shownTabs = ""
local shownPreset
local shownStatus = ""
local focusMode = ""
local building = false

local function notify(key)
    workpane.notify.error(translate("terminal.plugin.title"), translate(key))
end

-- The cursor blinks at the speed the reader chose, in milliseconds lit and then dark, only once the reader turned blinking on.
local function cursorBlink()
    return preferences.get("cursorBlink") and blinkIntervals[preferences.get("blinkSpeed")] or 0
end

-- A folder inside the home directory is written from a tilde, which is how a shell prompt shows it.
local function displayPath(path)
    local rest = path:sub(#home + 1)

    if path == home then
        return "~"
    end

    if path:sub(1, #home) == home and rest:match("^[/\\]") ~= nil then
        return "~" .. rest
    end

    return path
end

local function confirmClose(id)
    local session = workspace.session(id)

    if session == nil then
        return
    end

    local confirmed = workpane.await(workpane.dialogs.confirm({ title = translate("terminal.session.close-title"), message = translate("terminal.session.close-message", session.name), detail = translate("terminal.session.close-detail"), confirmText = translate("terminal.session.close-action"), destructive = true }))

    if confirmed then
        workspace.closeTerminal(id)
    end
end

local function rename(id)
    local session = workspace.session(id)

    if session == nil then
        return
    end

    local name = workpane.await(workpane.dialogs.prompt({ title = translate("terminal.session.rename-title"), message = translate("terminal.session.name"), value = session.name, confirmText = translate("terminal.actions.rename") }))

    if name ~= nil then
        workspace.renameTerminal(id, name)
    end
end

-- The plugin that receives a folder decides what to do with it, so a refusal is logged with its reason and the reader is told in this plugin's words.
local function offer(capability, id)
    local session = workspace.session(id)

    if session == nil then
        return
    end

    local _, failure = workpane.capabilities.request(capability, { path = session.directory }):await()

    if failure ~= nil then
        workpane.log.error("destination", "A plugin refused the directory of a terminal", { code = failure.code, detail = failure.detail })
        notify("terminal.error.destination-open")
    end
end

-- A file address shows its file in the file manager of the system, since only web addresses leave the product for a browser.
local function revealFile(url)
    local _, failure = workpane.system.revealPath(workpane.files.path(url)):await()

    if failure ~= nil then
        workpane.log.error("link", "A file named in a terminal could not be shown", { code = failure.code, detail = failure.detail })
        notify("terminal.error.address-open")
    end
end

-- An address opens in the Browser plugin unless the reader chose the system browser or nothing browses inside the product.
local function openLink(url)
    if url:sub(1, 7) == "file://" then
        revealFile(url)
        return
    end

    local external = preferences.get("openLinksExternally") or not workpane.capabilities.available(browserCapability)
    local future = external and workpane.system.openUrl(url) or workpane.capabilities.request(browserCapability, { url = url })
    local _, failure = future:await()

    if failure ~= nil then
        workpane.log.error("link", "An address printed in a terminal could not be opened", { code = failure.code, detail = failure.detail })
        notify("terminal.error.address-open")
    end
end

local function menuItems()
    local items = {
        { id = "restart", text = text("terminal.actions.restart-shell"), icon = "refresh" },
        { id = "shelf", text = text("terminal.actions.move-to-shelf"), icon = "shelf" },
        { id = "rename", text = text("terminal.actions.rename"), icon = "edit" },
    }

    if workpane.capabilities.available(editorCapability) then
        items[#items + 1] = { id = "editor", text = text("terminal.actions.open-in-editor"), icon = "folder" }
    end

    if workpane.capabilities.available(serverCapability) then
        items[#items + 1] = { id = "server", text = text("terminal.actions.serve-directory"), icon = "web-server" }
    end

    items[#items + 1] = { separator = true }
    items[#items + 1] = { id = "close", text = text("terminal.actions.close-terminal"), icon = "close", destructive = true }

    return items
end

-- Focus mode zooms the focused terminal, so zooming a terminal gives it the focus of the workspace and the keyboard stays with what is on screen.
local function toggleFocusMode(id)
    focusMode = focusMode == id and "" or id

    if focusMode ~= "" then
        workspace.focus(id)
    end

    view.render()
end

-- A directory that no longer exists cannot hold a shell, so the terminal starts again in the home folder and the reader is told why.
local function failed(pane, event)
    local session = workspace.session(pane.id)
    local details = { code = event.code, detail = event.message }

    if event.code == "terminal_directory_missing" and session ~= nil and session.directory ~= home then
        workpane.log.warning("session", "A terminal starts in the home folder because its folder is gone", details)
        notify("terminal.error.workdir-missing")
        workspace.setDirectory(pane.id, home)
        pane.terminal:set({ directory = home })
        pane.terminal:command("restart")
        return
    end

    local keys = { terminal_directory_missing = "terminal.error.directory-missing", terminal_shell_not_executable = "terminal.error.shell-not-executable" }
    workpane.log.error("session", "A terminal could not start its shell", details)
    pane.state = "failed"
    pane.exitLabel:set({ text = text(keys[event.code] or "terminal.error.spawn-failed") })
    view.render()
    notify(keys[event.code] or "terminal.error.spawn-failed")
end

local function restart(pane)
    pane.state = "running"
    pane.terminal:command("restart")
    view.render()
end

-- One pane per terminal, built once and moved between slots, the shelf and the other tabs for as long as the terminal exists.
local function pane(session)
    if panes[session.id] ~= nil then
        return panes[session.id]
    end

    local id = session.id
    local built = { id = id, state = "running", bell = false }

    built.terminal = ui.terminal({
        directory = session.directory,
        shell = session.shell,
        historyFile = workspace.historyFile(id),
        palette = preferences.get("palette"),
        fontSize = preferences.get("fontSize"),
        fontFamily = preferences.get("fontFamily"),
        clipboardWrites = preferences.get("clipboardWrites"),
        cursorBlink = cursorBlink(),
        grow = 1,
        height = 0,
        onFocus = function(event)
            if event.focused then
                built.bell = false
                workspace.focus(id)
                view.render()
            end
        end,
        onDirectory = function(event)
            workspace.setDirectory(id, event.path)
        end,
        onBell = function()
            local tab = workspace.current()
            built.bell = tab ~= nil and tab.focused ~= id
            view.render()
        end,
        onExit = function(event)
            built.state = "exited"
            built.exitLabel:set({ text = text("terminal.session.process-exited", tostring(event.code)) })
            view.render()
        end,
        onLink = function(event)
            openLink(event.url)
        end,
        onZoom = function(event)
            preferences.set("fontSize", math.tointeger(math.floor(event.fontSize + 0.5)))
        end,
        onError = function(event)
            failed(built, event)
        end,
        onInputRefused = function()
            workpane.log.warning("session", "A shell did not read the input already waiting for it", { id = id })
            notify("terminal.error.input-refused")
        end,
        onNotification = function(event)
            local current = workspace.session(id)
            workpane.notify.information(event.title ~= "" and event.title or (current ~= nil and current.name or ""), event.body)
        end,
    })

    built.dot = ui.statusIndicator({ tone = "neutral", size = 8 })
    built.title = ui.label({ text = "", style = "strong", color = "text-muted", wrap = false, grow = 1, minWidth = 40 })
    built.shellName = ui.label({ text = "", style = "muted", wrap = false, visible = false, collapse = 3 })
    built.bellIcon = ui.icon({ name = "bell", color = "warning", visible = false, tooltip = text("terminal.session.bell") })
    built.focusButton = ui.button({ icon = "focus", variant = "icon", tooltip = focusTooltip, collapse = 2, onClick = function()
        toggleFocusMode(id)
    end })

    built.actions = ui.menuButton({ icon = "more", variant = "icon", tooltip = text("terminal.actions.menu"), items = menuItems(), collapse = 1, onSelect = function(event)
        if event.item == "restart" then
            restart(built)
        elseif event.item == "shelf" then
            workspace.shelve(id)
        elseif event.item == "rename" then
            rename(id)
        elseif event.item == "editor" then
            offer(editorCapability, id)
        elseif event.item == "server" then
            offer(serverCapability, id)
        elseif event.item == "close" then
            confirmClose(id)
        end
    end })

    built.header = ui.row({ padding = { 0, 3, 0, 7 }, spacing = 6, background = "panel", drag = { kind = sessionKind, value = id, label = session.name } }, {
        built.dot,
        built.title,
        built.shellName,
        built.bellIcon,
        built.focusButton,
        built.actions,
        ui.button({ icon = "close", variant = "icon", tooltip = text("terminal.actions.close-terminal"), onClick = function()
            confirmClose(id)
        end }),
    })
    built.exitLabel = ui.label({ text = "", wrap = false, grow = 1 })
    built.exitBar = ui.row({ padding = { 4, 6, 4, 9 }, spacing = 8, background = "raised", visible = false }, {
        built.exitLabel,
        ui.button({ text = text("terminal.actions.restart"), icon = "refresh", variant = "toolbar", onClick = function()
            restart(built)
        end }),
    })

    -- A terminal dropped on a pane trades places with it, which is how two terminals swap slots.
    built.root = ui.column({ grow = 1, width = 0, height = 0, align = "stretch", accepts = { sessionKind }, onDrop = function(event)
        local tab = workspace.current()
        local slot = tab ~= nil and layouts.slotOf(tab, id) or nil

        if event.value ~= id and slot ~= nil then
            workspace.assign(event.value, slot)
        end
    end }, { built.header, built.terminal, built.exitBar })

    panes[id] = built

    return built
end

local function create(slot)
    workspace.createTerminal(slot)
end

local function emptySlot(slot)
    return ui.column({ grow = 1, width = 0, height = 0, align = "stretch", background = "window", justify = "center", spacing = 6, accepts = { sessionKind }, onDrop = function(event)
        workspace.assign(event.value, slot)
    end }, {
        ui.button({ text = text("terminal.actions.new-terminal"), icon = "add", align = "center", tooltip = text("terminal.slot.create"), onClick = function()
            create(slot)
        end }),
        ui.label({ text = text("terminal.slot.drop-session"), style = "muted", textAlign = "center", align = "center", wrap = false }),
    })
end

-- Places the slots of a preset in rows and columns, where the two presets with a large slot nest a column or a row inside the other.
local function arrange(preset, slots)
    if preset.id == "3-left" then
        return ui.row({ grow = 1, height = 0, spacing = 1 }, { slots[1], ui.column({ grow = 1, width = 0, height = 0, spacing = 1 }, { slots[2], slots[3] }) })
    end

    if preset.id == "3-bottom" then
        return ui.column({ grow = 1, height = 0, spacing = 1 }, { ui.row({ grow = 1, height = 0, spacing = 1 }, { slots[1], slots[2] }), slots[3] })
    end

    local rows = {}

    for row = 1, preset.rows do
        local cells = {}

        for column = 1, preset.columns do
            cells[column] = slots[(row - 1) * preset.columns + column] or ui.spacer({ grow = 1, width = 0 })
        end

        rows[row] = ui.row({ grow = 1, height = 0, spacing = 1 }, cells)
    end

    return ui.column({ grow = 1, height = 0, spacing = 1 }, rows)
end

local function chip(id)
    local session = workspace.session(id)

    return ui.card({ padding = { 0, 3, 0, 7 }, background = "raised", outline = "none", tooltip = text("terminal.shelf.instructions"), drag = { kind = sessionKind, value = id, label = session.name } }, {
        ui.row({ spacing = 5 }, {
            ui.button({ text = session.name, icon = "terminal", variant = "toolbar", onClick = function()
                workspace.showShelved(id)
            end }),
            ui.button({ icon = "close", variant = "icon", tooltip = text("terminal.actions.close-terminal"), onClick = function()
                confirmClose(id)
            end }),
        }),
    })
end

-- The board is built again only when what it shows changed, every terminal it keeps moves with its shell still running, and a workspace without tabs offers a new one.
local function rebuild(tab)
    local shown = {}
    local arrangement

    if tab == nil then
        arrangement = ui.column({ grow = 1, justify = "center", spacing = 14, background = "window" }, {
            ui.emptyState({ text = text("terminal.workspace.empty"), icon = "terminal" }),
            ui.button({ text = text("terminal.workspace.add"), icon = "workspace", variant = "primary", align = "center", onClick = view.createTerminal }),
        })
    elseif focusMode ~= "" then
        arrangement = ui.column({ grow = 1, height = 0 }, { pane(workspace.session(focusMode)).root })
        shown[focusMode] = true
    else
        local slots = {}

        for index, id in ipairs(tab.slots) do
            slots[index] = id == "" and emptySlot(index) or pane(workspace.session(id)).root
            shown[id] = true
        end

        arrangement = arrange(layouts.preset(tab.preset), slots)
    end

    local hidden = {}

    for _, session in ipairs(workspace.sessions()) do
        if not shown[session.id] then
            hidden[#hidden + 1] = pane(session).root
        end
    end

    for id in pairs(panes) do
        if workspace.session(id) == nil then
            panes[id] = nil
        end
    end

    controls.board:setChildren({ arrangement, ui.column({ visible = false }, hidden) })

    local chips = { controls.shelfIcon }
    local shelf = tab ~= nil and tab.shelf or {}

    for _, id in ipairs(shelf) do
        chips[#chips + 1] = chip(id)
    end

    controls.shelfChips:setChildren(chips)
    controls.shelf:set({ visible = #shelf > 0 })
    controls.layout:set({ visible = tab ~= nil })

    -- A terminal that moved lost the keyboard with its old place, so the focused one takes it again once the view is on screen, and only while it is shown.
    if tab ~= nil and tab.focused ~= "" and shown[tab.focused] and not building then
        panes[tab.focused].terminal:command("focus")
    end
end

-- A header is sent again only when something it shows changed, so a render that changes nothing costs nothing.
local function renderHeader(tab, built)
    local session = workspace.session(built.id)
    local focused = tab.focused == built.id
    local zoomed = focusMode == built.id
    local offered = tostring(workpane.capabilities.available(editorCapability)) .. tostring(workpane.capabilities.available(serverCapability))
    local shown = table.concat({ tostring(focused), built.state, session.name, session.directory, tostring(built.bell), tostring(zoomed), offered }, "|")

    if built.shown == shown then
        return
    end

    local tones = { running = focused and "accent" or "neutral", exited = "warning", failed = "danger" }
    built.shown = shown
    built.header:set({ background = focused and "hover" or "panel", drag = { kind = sessionKind, value = built.id, label = session.name } })
    built.dot:set({ tone = tones[built.state] })
    local shellName = session.shell:match("([^/\\]+)$"):gsub("%.[eE][xX][eE]$", "")
    built.title:set({ text = session.name .. "  " .. displayPath(session.directory), tooltip = session.name .. "\n" .. session.directory, color = focused and "text" or "text-muted" })
    built.shellName:set({ text = shellName, visible = session.name ~= shellName })
    built.bellIcon:set({ visible = built.bell })
    built.focusButton:set({ icon = zoomed and "restore" or "focus", tooltip = zoomed and restoreTooltip or focusTooltip })
    built.actions:set({ items = menuItems() })
    built.exitBar:set({ visible = built.state == "exited" or built.state == "failed" })
end

-- The tabs are sent again only when a tab, its name, its order or the selection changed.
local function renderTabs(tab)
    local items = {}
    local parts = { tab ~= nil and tab.id or "" }

    for index, candidate in ipairs(workspace.tabs()) do
        items[index] = { id = candidate.id, text = candidate.name, icon = "terminal" }
        parts[#parts + 1] = candidate.id .. "\0" .. candidate.name
    end

    local signature = table.concat(parts, "\n")

    if signature ~= shownTabs then
        shownTabs = signature
        controls.tabs:set({ items = items, current = tab ~= nil and tab.id or "" })
    end
end

-- The swatches and the status bar are sent again only when the layout, the counts or the folder they show changed.
local function renderFooter(tab)
    local preset = tab ~= nil and tab.preset or ""

    if preset ~= shownPreset then
        shownPreset = preset

        for id, swatch in pairs(swatches) do
            swatch:set({ checked = id == preset })
        end
    end

    local focused = tab ~= nil and workspace.session(tab.focused) or nil
    local directory = focused ~= nil and focused.directory or ""
    local status = table.concat({ #workspace.tabs(), #workspace.sessions(), directory }, "\0")

    if status == shownStatus then
        return
    end

    shownStatus = status
    local workspaces = #workspace.tabs() == 1 and text("terminal.status.workspace-single") or text("terminal.status.workspace-multiple", number(#workspace.tabs(), 0))
    local terminals = #workspace.sessions() == 1 and text("terminal.status.single") or text("terminal.status.multiple", number(#workspace.sessions(), 0))
    controls.status:set({ text = text("terminal.status.summary", workspaces, terminals) })
    controls.directory:set({ text = directory })
end

-- Everything on screen follows the workspace and is sent only when it changed, and the board and the shelf are rebuilt only when their contents or the layout changed.
-- Focus mode leaves once another terminal takes the focus, such as one the reader created or brought back from the shelf.
function view.render()
    if controls == nil then
        return
    end

    local tab = workspace.current()
    renderTabs(tab)

    if focusMode ~= "" and (tab == nil or tab.focused ~= focusMode) then
        focusMode = ""
    end

    local sessions = {}

    for index, session in ipairs(workspace.sessions()) do
        sessions[index] = session.id
    end

    local current = tab == nil and "empty" or table.concat({ tab.id, tab.preset, table.concat(tab.slots, ","), table.concat(tab.shelf, ","), table.concat(sessions, ","), focusMode }, "|")

    if current ~= shape then
        shape = current
        rebuild(tab)
    end

    for _, built in pairs(panes) do
        renderHeader(tab, built)
    end

    renderFooter(tab)
end

-- A setting reaches every terminal at once, including the ones on the shelf and in the other tabs, and the blinking of the cursor follows two of them.
function view.apply(key, value)
    local changes = { fontSize = { fontSize = value }, fontFamily = { fontFamily = value }, palette = { palette = value }, clipboardWrites = { clipboardWrites = value }, cursorBlink = { cursorBlink = cursorBlink() }, blinkSpeed = { cursorBlink = cursorBlink() } }
    local change = changes[key]

    if change == nil then
        return
    end

    for _, built in pairs(panes) do
        built.terminal:set(change)
    end
end

function view.createTerminal()
    create(nil)
end

function view.createTab()
    workspace.createTab()
end

function view.closeFocused()
    local tab = workspace.current()

    if tab ~= nil and tab.focused ~= "" then
        confirmClose(tab.focused)
    end
end

function view.openLayouts()
    if workspace.current() ~= nil then
        controls.layout:command("open")
    end
end

local function renameTab(event)
    local tab

    for _, candidate in ipairs(workspace.tabs()) do
        tab = candidate.id == event.id and candidate or tab
    end

    if tab == nil then
        return
    end

    local name = workpane.await(workpane.dialogs.prompt({ title = translate("terminal.tabs.rename-title"), message = translate("terminal.tabs.name"), value = tab.name, confirmText = translate("terminal.actions.rename") }))

    if name ~= nil then
        workspace.renameTab(tab.id, name)
    end
end

local function closeTab(event)
    local tab

    for _, candidate in ipairs(workspace.tabs()) do
        tab = candidate.id == event.id and candidate or tab
    end

    if tab == nil then
        return
    end

    local confirmed = workpane.await(workpane.dialogs.confirm({ title = translate("terminal.tabs.close-title"), message = translate("terminal.tabs.close-message", tab.name), detail = translate("terminal.tabs.close-detail"), confirmText = translate("terminal.tabs.close-action"), destructive = true }))

    if confirmed then
        workspace.closeTab(tab.id)
    end
end

local function layoutChooser()
    local choices = {}

    for index, preset in ipairs(layouts.presets()) do
        local name = text("terminal.layout." .. preset.id)
        local tooltip = preset.slots == 1 and text("terminal.layout.slot-single", name) or text("terminal.layout.slots", name, number(preset.slots, 0))

        swatches[preset.id] = ui.layoutSwatch({ columns = preset.columns, rows = preset.rows, cells = layouts.cells(preset), tooltip = tooltip, onClick = function()
            controls.layout:command("close")
            workspace.changeLayout(preset.id)
        end })

        choices[index] = swatches[preset.id]
    end

    return ui.grid({ columns = 4, columnSpacing = swatchSpacing, rowSpacing = swatchSpacing, width = swatchWidth * 4 + swatchSpacing * 3 }, choices)
end

function view.build()
    home = workpane.system.home()
    controls = {
        tabs = ui.tabs({ items = {}, closable = true, addButton = true, movable = true, grow = 1, onSelect = function(event)
            workspace.select(event.id)
        end, onClose = closeTab, onAdd = view.createTab, onMove = function(event)
            workspace.moveTab(event.id, event.index)
        end, onActivate = renameTab }),
        board = ui.column({ grow = 1, height = 0, background = "raised" }, {}),
        shelfIcon = ui.icon({ name = "shelf", color = "text-muted", tooltip = text("terminal.shelf.outside-active-layout") }),
        status = ui.label({ text = "", style = "caption", wrap = false }),
        directory = ui.label({ text = "", style = "caption", textAlign = "end", wrap = false, grow = 1 }),
    }
    controls.layout = ui.popover({ text = text("terminal.actions.layout"), icon = "layout", variant = "toolbar" }, layoutChooser())
    -- The shelf scrolls sideways when its terminals do not fit, so every one of them stays within reach.
    controls.shelfChips = ui.row({ spacing = 7 }, { controls.shelfIcon })
    controls.shelf = ui.row({ padding = { 4, 8 }, background = "panel", borders = { "top" }, visible = false, accepts = { sessionKind }, onDrop = function(event)
        workspace.shelve(event.value)
    end }, { ui.scroll({ orientation = "horizontal", grow = 1 }, controls.shelfChips) })

    local root = ui.column({}, {
        ui.row({ padding = { 0, 5, 0, 0 }, spacing = 2, background = "panel", borders = { "bottom" } }, {
            controls.tabs,
            ui.button({ text = text("terminal.actions.new-terminal"), icon = "terminal", variant = "toolbar", onClick = view.createTerminal }),
            controls.layout,
        }),
        controls.board,
        controls.shelf,
        ui.row({ padding = { 4, 10 }, spacing = 10, background = "panel", borders = { "top" } }, { controls.status, controls.directory }),
    })

    shape = ""
    shownTabs = ""
    shownPreset = nil
    shownStatus = ""
    building = true
    view.render()
    building = false

    return root
end

return view
