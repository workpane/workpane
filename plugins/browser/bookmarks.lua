-- Keeps the bookmarks in ordered groups, with the ungrouped ones in a collection of their own, and writes them whole after every change.
local crypto = require("crypto")
local address = include("address")
local session = include("session")
local store = include("store")

local translate = workpane.i18n.translate

local bookmarks = {}

local groups = {}
local entries = {}
local committedGroups = {}
local committedEntries = {}
local revision = 0
local listener

local function copy(list)
    local copied = {}

    for index, item in ipairs(list) do
        local duplicate = {}

        for key, value in pairs(item) do
            duplicate[key] = value
        end

        copied[index] = duplicate
    end

    return copied
end

local function invalid(detail)
    workpane.database.invalid(detail)
end

local function refused(code, detail)
    return { code = code, message = "The bookmark information is invalid", detail = tostring(detail) }
end

local function trim(value)
    return type(value) == "string" and value:match("^%s*(.-)%s*$") or ""
end

local function changed()
    if listener ~= nil then
        listener()
    end
end

-- The whole list is written after every change, and a write that fails while it is still the latest puts the committed bookmarks back.
local function persist()
    revision = revision + 1
    local written = revision
    local candidateGroups = copy(groups)
    local candidateEntries = copy(entries)

    workpane.task(function()
        local _, failure = store.saveBookmarks(candidateGroups, candidateEntries):await()

        if failure == nil then
            committedGroups, committedEntries = candidateGroups, candidateEntries
            return
        end

        if written == revision then
            groups, entries = copy(committedGroups), copy(committedEntries)
            changed()
        end

        workpane.notify.error(translate("browser.plugin.title"), translate("browser.error.persistence"))
    end)

    changed()
end

-- Groups and bookmarks are checked whole before any of them is shown, each bookmark against the group it names and the position its collection expects.
function bookmarks.restore()
    local groupRows = store.groups()
    local known = {}

    for index, row in ipairs(groupRows) do
        if type(row.id) ~= "string" or row.id == "" or known[row.id] or trim(row.name) == "" or trim(row.name) ~= row.name or row.position ~= index - 1 then
            invalid(tostring(row.id))
        end

        if not session.timestamp(row.created_at_utc) or not session.timestamp(row.updated_at_utc) or row.updated_at_utc < row.created_at_utc then
            invalid(row.id)
        end

        known[row.id] = true
        groups[index] = { id = row.id, name = row.name, createdAt = row.created_at_utc, updatedAt = row.updated_at_utc }
    end

    local rows = store.bookmarks()
    local seen = {}
    local expected = {}

    for index, row in ipairs(rows) do
        local container = row.group_id or ""

        if type(row.id) ~= "string" or row.id == "" or seen[row.id] or (row.group_id ~= nil and not known[row.group_id]) or trim(row.name) == "" or trim(row.name) ~= row.name then
            invalid(tostring(row.id))
        end

        if address.normalize(row.url) ~= row.url or row.position ~= (expected[container] or 0) or not session.timestamp(row.created_at_utc) or not session.timestamp(row.updated_at_utc) or row.updated_at_utc < row.created_at_utc then
            invalid(row.id)
        end

        seen[row.id] = true
        expected[container] = row.position + 1
        entries[index] = { id = row.id, groupId = row.group_id, name = row.name, url = row.url, createdAt = row.created_at_utc, updatedAt = row.updated_at_utc }
    end

    committedGroups, committedEntries = copy(groups), copy(entries)
end

function bookmarks.listen(handler)
    listener = handler
end

function bookmarks.groups()
    return groups
end

function bookmarks.entries()
    return entries
end

function bookmarks.findGroup(id)
    for index, group in ipairs(groups) do
        if group.id == id then
            return group, index
        end
    end

    return nil, nil
end

function bookmarks.find(id)
    for index, entry in ipairs(entries) do
        if entry.id == id then
            return entry, index
        end
    end

    return nil, nil
end

function bookmarks.createGroup(name)
    local trimmed = trim(name)

    if trimmed == "" then
        return false, refused("browser_bookmark_group_name_invalid", name)
    end

    local now = workpane.time.now()
    groups[#groups + 1] = { id = crypto.uuidV4(), name = trimmed, createdAt = now, updatedAt = now }
    persist()

    return true, nil
end

function bookmarks.renameGroup(id, name)
    local group = bookmarks.findGroup(id)
    local trimmed = trim(name)

    if group == nil then
        return false, refused("browser_bookmark_group_unknown", id)
    end

    if trimmed == "" then
        return false, refused("browser_bookmark_group_name_invalid", name)
    end

    if group.name ~= trimmed then
        group.name = trimmed
        session.touch(group)
        persist()
    end

    return true, nil
end

-- A removed group hands its bookmarks to the ungrouped collection in the order they already had.
function bookmarks.removeGroup(id)
    local group, index = bookmarks.findGroup(id)

    if group == nil then
        return false, refused("browser_bookmark_group_unknown", id)
    end

    table.remove(groups, index)

    for _, entry in ipairs(entries) do
        if entry.groupId == id then
            entry.groupId = nil
            session.touch(entry)
        end
    end

    persist()

    return true, nil
end

local function checked(name, url, groupId)
    local trimmed = trim(name)
    local normalized = address.normalize(url)

    if trimmed == "" then
        return nil, nil, "browser_bookmark_name_invalid"
    end

    if normalized == nil then
        return nil, nil, "browser_address_invalid"
    end

    if groupId ~= nil and bookmarks.findGroup(groupId) == nil then
        return nil, nil, "browser_bookmark_group_unknown"
    end

    return trimmed, normalized, nil
end

function bookmarks.create(name, url, groupId)
    local trimmed, normalized, code = checked(name, url, groupId)

    if code ~= nil then
        return nil, refused(code, name)
    end

    local now = workpane.time.now()
    local id = crypto.uuidV4()
    entries[#entries + 1] = { id = id, groupId = groupId, name = trimmed, url = normalized, createdAt = now, updatedAt = now }
    persist()

    return id, nil
end

function bookmarks.update(id, name, url, groupId)
    local entry = bookmarks.find(id)
    local trimmed, normalized, code = checked(name, url, groupId)

    if entry == nil then
        return false, refused("browser_bookmark_unknown", id)
    end

    if code ~= nil then
        return false, refused(code, name)
    end

    if entry.name == trimmed and entry.url == normalized and entry.groupId == groupId then
        return true, nil
    end

    entry.name, entry.url, entry.groupId = trimmed, normalized, groupId
    session.touch(entry)
    persist()

    return true, nil
end

function bookmarks.remove(id)
    local entry, index = bookmarks.find(id)

    if entry == nil then
        return false, refused("browser_bookmark_unknown", id)
    end

    table.remove(entries, index)
    persist()

    return true, nil
end

-- A bookmark dropped into a collection lands before the bookmark that held that place in it, or after its last bookmark.
function bookmarks.place(id, groupId, position)
    local entry, index = bookmarks.find(id)

    if entry == nil or (groupId ~= nil and bookmarks.findGroup(groupId) == nil) or math.type(position) ~= "integer" or position < 0 then
        return false, refused("browser_bookmark_layout_invalid", id)
    end

    table.remove(entries, index)
    local count = 0
    local slot = #entries + 1

    for candidate, other in ipairs(entries) do
        if other.groupId == groupId then
            if count == position then
                slot = candidate
                break
            end

            count = count + 1
            slot = candidate + 1
        end
    end

    if entry.groupId ~= groupId then
        entry.groupId = groupId
        session.touch(entry)
    end

    table.insert(entries, slot, entry)
    persist()

    return true, nil
end

return bookmarks
