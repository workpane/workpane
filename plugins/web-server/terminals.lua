-- Follows the terminals of the Terminal plugin, whose messages are checked whole before any of them changes what the servers know.
local servers = include("servers")

local translate = workpane.i18n.translate

local terminals = {}

local snapshotCapability = "terminal.workspace.snapshot"

local function invalid()
    workpane.notify.error(translate("web-server.plugin.title"), translate("web-server.error.message-invalid"))
end

local function exactly(value, keys)
    if type(value) ~= "table" then
        return false
    end

    local count = 0

    for key in pairs(value) do
        count = count + 1

        if keys[key] ~= "string" or type(value[key]) ~= "string" then
            return false
        end
    end

    for _ in pairs(keys) do
        count = count - 1
    end

    return count == 0
end

-- A snapshot names every open terminal once and the active one among them, or an empty active terminal when none is selected.
local function readSnapshot(snapshot)
    if type(snapshot) ~= "table" or type(snapshot.activeTerminalId) ~= "string" or type(snapshot.terminals) ~= "table" then
        return nil
    end

    for key in pairs(snapshot) do
        if key ~= "activeTerminalId" and key ~= "terminals" then
            return nil
        end
    end

    local list = {}
    local known = {}

    for index, terminal in ipairs(snapshot.terminals) do
        if not exactly(terminal, { id = "string", name = "string", cwd = "string" }) or terminal.id == "" or known[terminal.id] then
            return nil
        end

        known[terminal.id] = true
        list[index] = { id = terminal.id, name = terminal.name, cwd = terminal.cwd }
    end

    if snapshot.activeTerminalId ~= "" and not known[snapshot.activeTerminalId] then
        return nil
    end

    return { active = snapshot.activeTerminalId, list = list }
end

function terminals.synchronize()
    if not workpane.capabilities.available(snapshotCapability) then
        return
    end

    local snapshot, failure = workpane.capabilities.request(snapshotCapability, {}):await()

    if failure ~= nil then
        workpane.notify.error(translate("web-server.plugin.title"), translate("web-server.error.terminal-message"))
        return
    end

    local read = readSnapshot(snapshot)

    if read == nil then
        invalid()
        return
    end

    servers.setTerminals(read.active, read.list)
end

-- The Terminal plugin announces a change without details, so the plugin asks for a new snapshot, and only the Terminal publishes under its own topics.
function terminals.changed(payload)
    if type(payload) ~= "table" or next(payload) ~= nil then
        invalid()
        return
    end

    terminals.synchronize()
end

function terminals.closed(payload)
    if not exactly(payload, { terminalId = "string" }) or payload.terminalId == "" then
        invalid()
        return
    end

    servers.unlinkTerminal(payload.terminalId)
end

return terminals
