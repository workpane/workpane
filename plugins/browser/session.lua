-- Keeps the open tabs in order with exactly one of them active, and writes the whole session after every change with the last write deciding.
local crypto = require("crypto")
local address = include("address")
local store = include("store")

local translate = workpane.i18n.translate

local session = {}

local tabs = {}
local committed = {}
local revision = 0
local listener

local function copy(list)
    local copied = {}

    for index, tab in ipairs(list) do
        copied[index] = { id = tab.id, title = tab.title, url = tab.url, createdAt = tab.createdAt, updatedAt = tab.updatedAt, active = tab.active }
    end

    return copied
end

local function invalid(detail)
    workpane.database.invalid(detail)
end

-- A stored moment is the UTC form the host writes, with every field inside its calendar range.
function session.timestamp(value)
    local year, month, day, hour, minute, second = (type(value) == "string" and value or ""):match("^(%d%d%d%d)%-(%d%d)%-(%d%d)T(%d%d):(%d%d):(%d%d)%.%d%d%dZ$")
    return year ~= nil and tonumber(month) >= 1 and tonumber(month) <= 12 and tonumber(day) >= 1 and tonumber(day) <= 31 and tonumber(hour) < 24 and tonumber(minute) < 60 and tonumber(second) < 60
end

-- Stamps a change of a tab, a group or a bookmark never earlier than its creation, so a clock set back never leaves a row the next start refuses.
function session.touch(record)
    local now = workpane.time.now()
    record.updatedAt = now > record.createdAt and now or record.createdAt
end

-- The whole session is written in one transaction, and a write that fails while it is still the latest puts the committed session back.
local function persist()
    revision = revision + 1
    local written = revision
    local candidate = copy(tabs)

    workpane.task(function()
        local _, failure = store.saveTabs(candidate):await()

        if failure == nil then
            committed = candidate
            return
        end

        if written == revision then
            tabs = copy(committed)

            if listener ~= nil then
                listener()
            end
        end

        workpane.notify.error(translate("browser.plugin.title"), translate("browser.error.persistence"))
    end)
end

-- The stored session is checked whole before any tab of it opens, and a start with nothing stored opens one tab on the homepage.
function session.restore(homepage)
    local rows = store.tabs()
    local seen = {}
    local active = 0

    for index, row in ipairs(rows) do
        local title = type(row.title) == "string" and row.title:match("^%s*(.-)%s*$") or ""

        if type(row.id) ~= "string" or row.id == "" or seen[row.id] or title == "" or title ~= row.title or address.normalize(row.url) ~= row.url then
            invalid(tostring(row.id))
        end

        if row.position ~= index - 1 or (row.active ~= 0 and row.active ~= 1) or not session.timestamp(row.created_at_utc) or not session.timestamp(row.updated_at_utc) or row.updated_at_utc < row.created_at_utc then
            invalid(row.id)
        end

        seen[row.id] = true
        active = active + row.active
        tabs[index] = { id = row.id, title = row.title, url = row.url, createdAt = row.created_at_utc, updatedAt = row.updated_at_utc, active = row.active == 1 }
    end

    if #tabs > 0 and active ~= 1 then
        invalid("active")
    end

    committed = copy(tabs)

    if #tabs == 0 then
        local now = workpane.time.now()
        tabs[1] = { id = crypto.uuidV4(), title = translate("browser.tabs.new"), url = homepage, createdAt = now, updatedAt = now, active = true }
        workpane.await(store.saveTabs(tabs))
        committed = copy(tabs)
    end
end

-- The listener hears a session that was put back after a write failed, which is the one change the view did not ask for.
function session.listen(handler)
    listener = handler
end

function session.list()
    return tabs
end

function session.find(id)
    for index, tab in ipairs(tabs) do
        if tab.id == id then
            return tab, index
        end
    end

    return nil, nil
end

function session.active()
    for _, tab in ipairs(tabs) do
        if tab.active then
            return tab
        end
    end

    return nil
end

-- A new tab is active when asked or when it is the only one, because a session with tabs always has one active.
function session.create(url, activate)
    local normalized = address.normalize(url)

    if normalized == nil then
        return nil, { code = "browser_address_invalid", message = "The browser address is invalid", detail = tostring(url) }
    end

    local becomesActive = activate or #tabs == 0

    if becomesActive then
        for _, tab in ipairs(tabs) do
            tab.active = false
        end
    end

    local now = workpane.time.now()
    local id = crypto.uuidV4()
    tabs[#tabs + 1] = { id = id, title = translate("browser.tabs.new"), url = normalized, createdAt = now, updatedAt = now, active = becomesActive }
    persist()

    return id, nil
end

-- Closing the active tab activates its neighbour, and closing the last tab leaves no tab at all.
function session.close(id)
    local tab, index = session.find(id)

    if tab == nil then
        return false
    end

    table.remove(tabs, index)

    if tab.active and #tabs > 0 then
        tabs[math.min(index, #tabs)].active = true
    end

    persist()

    return true
end

function session.activate(id)
    local selected = session.find(id)

    if selected == nil or selected.active then
        return selected ~= nil
    end

    for _, tab in ipairs(tabs) do
        tab.active = tab.id == id
    end

    session.touch(selected)
    persist()

    return true
end

function session.move(id, position)
    local tab, index = session.find(id)

    if tab == nil or math.type(position) ~= "integer" or position < 0 or position >= #tabs then
        return false
    end

    table.insert(tabs, position + 1, table.remove(tabs, index))
    persist()

    return true
end

-- A page that arrived somewhere keeps its address and its title in one write, and answers whether either changed.
function session.update(id, url, title)
    local tab = session.find(id)
    local normalized = address.normalize(url)
    local trimmed = type(title) == "string" and title:match("^%s*(.-)%s*$") or ""

    if tab == nil then
        return false
    end

    local moved = normalized ~= nil and tab.url ~= normalized
    local renamed = trimmed ~= "" and tab.title ~= trimmed

    if not moved and not renamed then
        return false
    end

    tab.url = moved and normalized or tab.url
    tab.title = renamed and trimmed or tab.title
    session.touch(tab)
    persist()

    return true
end

return session
