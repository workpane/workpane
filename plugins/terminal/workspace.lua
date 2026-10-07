-- Keeps every workspace tab and terminal as one document, checked whole when it is read and written whole after every change.
local crypto = require("crypto")
local fs = require("fs")
local json = require("json")
local layouts = include("layouts")
local store = include("store")

local translate = workpane.i18n.translate

local workspace = {}

local document
local listener
local announced

local function invalid(detail)
    workpane.database.invalid(detail)
end

local function exactly(value, keys)
    if type(value) ~= "table" then
        return false
    end

    for key in pairs(value) do
        if not keys[key] then
            return false
        end
    end

    for key in pairs(keys) do
        if value[key] == nil then
            return false
        end
    end

    return true
end

local function isList(value)
    if type(value) ~= "table" then
        return false
    end

    local count = 0

    for _ in pairs(value) do
        count = count + 1
    end

    return count == #value
end

local function trimmed(value)
    return type(value) == "string" and value ~= "" and value:match("^%s*(.-)%s*$") == value
end

-- Every terminal is named once and placed exactly once, in a slot or on the shelf of one tab, each tab focuses a terminal it shows, and a workspace whose tabs the reader closed selects none.
local function validate(candidate)
    if not exactly(candidate, { selected = true, tabs = true, sessions = true }) or type(candidate.selected) ~= "string" or not isList(candidate.tabs) or not isList(candidate.sessions) then
        invalid("document")
    end

    local sessions = {}

    for _, session in ipairs(candidate.sessions) do
        if not exactly(session, { id = true, name = true, directory = true, shell = true }) or not trimmed(session.id) or sessions[session.id] or not trimmed(session.name) or not workpane.files.absolute(session.directory) or not workpane.files.absolute(session.shell) then
            invalid("session")
        end

        sessions[session.id] = true
    end

    local tabs = {}
    local placed = {}

    for _, tab in ipairs(candidate.tabs) do
        local preset = type(tab) == "table" and layouts.preset(tab.preset) or nil

        if not exactly(tab, { id = true, name = true, preset = true, focused = true, slots = true, shelf = true }) or not trimmed(tab.id) or tabs[tab.id] or not trimmed(tab.name) or preset == nil or type(tab.focused) ~= "string" then
            invalid("tab")
        end

        if not isList(tab.slots) or #tab.slots ~= preset.slots or not isList(tab.shelf) then
            invalid(tab.id)
        end

        tabs[tab.id] = true
        local shown = false

        for _, id in ipairs(tab.slots) do
            if type(id) ~= "string" or (id ~= "" and (not sessions[id] or placed[id])) then
                invalid(tab.id)
            end

            if id ~= "" then
                placed[id] = true
                shown = true
            end
        end

        for _, id in ipairs(tab.shelf) do
            if type(id) ~= "string" or not sessions[id] or placed[id] then
                invalid(tab.id)
            end

            placed[id] = true
        end

        if (shown and layouts.slotOf(tab, tab.focused) == nil) or (not shown and tab.focused ~= "") then
            invalid(tab.id)
        end
    end

    for id in pairs(sessions) do
        if not placed[id] then
            invalid(id)
        end
    end

    if (#candidate.tabs > 0 and not tabs[candidate.selected]) or (#candidate.tabs == 0 and candidate.selected ~= "") then
        invalid("selected")
    end
end

-- The whole document is written after every change, and the database thread applies the writes in the order they were made.
local function persist()
    local text = json.encode(document)

    workpane.task(function()
        local _, failure = store.write(text):await()

        if failure ~= nil then
            workpane.log.error("workspace", "The terminal workspace could not be saved", { code = failure.code, detail = failure.detail })
            workpane.notify.error(translate("terminal.error.save-title"), translate("terminal.error.workspace-invalid"))
        end
    end)
end

-- The announcement carries nothing because other plugins ask for the snapshot they need, so it goes out only when that snapshot changed.
local function announce()
    local snapshot = workspace.snapshot()
    local parts = { snapshot.activeTerminalId }

    for _, terminal in ipairs(snapshot.terminals) do
        parts[#parts + 1] = table.concat({ terminal.id, terminal.name, terminal.cwd }, "\0")
    end

    local signature = table.concat(parts, "\n")

    if signature == announced then
        return
    end

    announced = signature
    workpane.events.publish("terminal.workspace.changed", {})
end

local function changed()
    persist()

    if listener ~= nil then
        listener()
    end

    announce()
end

-- A new terminal starts the shell of the reader and keeps that shell, so it comes back with the same one after the reader changes the default.
local function newSession()
    local shell = workpane.system.shell()
    local session = { id = crypto.uuidV4(), name = shell.name, directory = workpane.system.home(), shell = shell.path }
    document.sessions[#document.sessions + 1] = session

    return session
end

local function newTab()
    return { id = crypto.uuidV4(), name = translate("terminal.workspace.numbered", #document.tabs + 1), preset = "1-single", focused = "", slots = { "" }, shelf = {} }
end

-- A history file whose terminal the workspace no longer holds goes, so a terminal the reader closed leaves nothing behind, and a folder not written yet holds none.
local function removeStaleHistories()
    local folder = workpane.app.dataDirectory .. "/terminal/history"
    local names = fs.readdir(folder):await() or {}

    for _, name in ipairs(names) do
        local id = name:match("^(.+)%.history$")

        if id ~= nil and workspace.session(id) == nil then
            fs.removeRecursive(folder .. "/" .. name):await()
        end
    end
end

-- The stored workspace is checked whole before any terminal of it starts, and a first start opens one tab with one terminal in the home folder.
function workspace.load()
    local stored = store.read()

    if stored ~= nil then
        local decoded, candidate = pcall(json.decode, stored)

        if not decoded then
            invalid("json")
        end

        validate(candidate)
        document = candidate
        removeStaleHistories()
        return
    end

    document = { selected = "", tabs = {}, sessions = {} }
    local tab = newTab()
    local session = newSession()
    tab.slots[1] = session.id
    tab.focused = session.id
    document.tabs[1] = tab
    document.selected = tab.id
    workpane.await(store.write(json.encode(document)))
end

function workspace.listen(handler)
    listener = handler
end

function workspace.tabs()
    return document.tabs
end

function workspace.sessions()
    return document.sessions
end

-- Answers the selected tab, or nil once the reader closed every tab.
function workspace.current()
    for _, tab in ipairs(document.tabs) do
        if tab.id == document.selected then
            return tab
        end
    end

    return nil
end

-- Each terminal keeps the history of its shell in a file of its own under the data directory of the product.
function workspace.historyFile(id)
    return workpane.app.dataDirectory .. "/terminal/history/" .. id .. ".history"
end

function workspace.session(id)
    for _, session in ipairs(document.sessions) do
        if session.id == id then
            return session
        end
    end

    return nil
end

local function tabIndex(id)
    for index, tab in ipairs(document.tabs) do
        if tab.id == id then
            return index
        end
    end

    return nil
end

-- The active terminal is the focused terminal of the selected tab, and every terminal is named with the folder its shell stands in.
function workspace.snapshot()
    local terminals = {}

    for index, session in ipairs(document.sessions) do
        terminals[index] = { id = session.id, name = session.name, cwd = session.directory }
    end

    local tab = workspace.current()

    return { activeTerminalId = tab ~= nil and tab.focused or "", terminals = terminals }
end

function workspace.select(id)
    if tabIndex(id) == nil or document.selected == id then
        return
    end

    document.selected = id
    changed()
end

local function openTab()
    local tab = newTab()
    document.tabs[#document.tabs + 1] = tab
    document.selected = tab.id

    return tab
end

function workspace.createTab()
    openTab()
    changed()
end

function workspace.renameTab(id, name)
    local index = tabIndex(id)
    local trimmedName = type(name) == "string" and name:match("^%s*(.-)%s*$") or ""

    if index == nil or trimmedName == "" then
        return
    end

    document.tabs[index].name = trimmedName
    changed()
end

function workspace.moveTab(id, position)
    local index = tabIndex(id)

    if index == nil or math.type(position) ~= "integer" or position < 0 or position >= #document.tabs then
        return
    end

    table.insert(document.tabs, position + 1, table.remove(document.tabs, index))
    changed()
end

local function forget(ids)
    for index = #document.sessions, 1, -1 do
        if ids[document.sessions[index].id] then
            table.remove(document.sessions, index)
        end
    end
end

-- Closing a tab ends every terminal it holds, and closing the last one leaves the workspace without tabs until the reader opens another one.
function workspace.closeTab(id)
    local index = tabIndex(id)

    if index == nil then
        return
    end

    local tab = table.remove(document.tabs, index)
    local closed = {}
    local ids = {}

    for _, assigned in ipairs(tab.slots) do
        if assigned ~= "" then
            closed[#closed + 1] = assigned
            ids[assigned] = true
        end
    end

    for _, shelved in ipairs(tab.shelf) do
        closed[#closed + 1] = shelved
        ids[shelved] = true
    end

    forget(ids)

    if document.selected == tab.id then
        document.selected = #document.tabs > 0 and document.tabs[math.min(index, #document.tabs)].id or ""
    end

    for _, closedId in ipairs(closed) do
        workpane.events.publish("terminal.session.closed", { terminalId = closedId })
    end

    changed()
end

-- A new terminal takes the slot asked for when it is empty, then the first empty slot, then the place of the focused terminal, which moves to the shelf, and it opens a tab of its own when no tab is left.
function workspace.createTerminal(slot)
    local tab = workspace.current() or openTab()
    local session = newSession()
    local target = slot ~= nil and tab.slots[slot] == "" and slot or layouts.firstEmpty(tab) or layouts.slotOf(tab, tab.focused)
    layouts.assign(tab, session.id, target)
    tab.focused = session.id
    changed()

    return session.id
end

function workspace.closeTerminal(id)
    if workspace.session(id) == nil then
        return
    end

    for _, tab in ipairs(document.tabs) do
        if layouts.contains(tab, id) then
            layouts.remove(tab, id)
            layouts.normalizeFocus(tab)
        end
    end

    forget({ [id] = true })
    workpane.events.publish("terminal.session.closed", { terminalId = id })
    changed()
end

function workspace.renameTerminal(id, name)
    local session = workspace.session(id)
    local trimmedName = type(name) == "string" and name:match("^%s*(.-)%s*$") or ""

    if session == nil or trimmedName == "" or session.name == trimmedName then
        return
    end

    session.name = trimmedName
    changed()
end

function workspace.setDirectory(id, path)
    local session = workspace.session(id)

    if session == nil or not workpane.files.absolute(path) or session.directory == path then
        return
    end

    session.directory = path
    changed()
end

function workspace.changeLayout(presetId)
    local tab = workspace.current()
    local preset = layouts.preset(presetId)

    if tab == nil or preset == nil or tab.preset == presetId then
        return
    end

    layouts.change(tab, preset)
    layouts.normalizeFocus(tab)
    changed()
end

-- Only a terminal of the selected tab moves inside it, into a slot of its layout.
function workspace.assign(id, slot)
    local tab = workspace.current()

    if tab == nil or not layouts.contains(tab, id) or math.type(slot) ~= "integer" or slot < 1 or slot > #tab.slots then
        return
    end

    layouts.assign(tab, id, slot)
    tab.focused = id
    changed()
end

-- A shelved terminal comes back into the first empty slot, or takes the place of the focused terminal.
function workspace.showShelved(id)
    local tab = workspace.current()

    if tab == nil or not layouts.contains(tab, id) or layouts.slotOf(tab, id) ~= nil then
        return
    end

    layouts.assign(tab, id, layouts.firstEmpty(tab) or layouts.slotOf(tab, tab.focused))
    tab.focused = id
    changed()
end

function workspace.shelve(id)
    local tab = workspace.current()

    if tab == nil or not layouts.contains(tab, id) or layouts.slotOf(tab, id) == nil then
        return
    end

    layouts.shelve(tab, id)
    layouts.normalizeFocus(tab)
    changed()
end

function workspace.focus(id)
    local tab = workspace.current()

    if tab == nil or tab.focused == id or layouts.slotOf(tab, id) == nil then
        return
    end

    tab.focused = id
    changed()
end

return workspace
