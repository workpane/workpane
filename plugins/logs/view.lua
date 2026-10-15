-- Shows the stored log newest first, one page at a time, narrowed by a search and a level without waiting on the database.
local async = require("async")
local store = include("store")

local ui = workpane.ui
local text = workpane.i18n.text
local translate = workpane.i18n.translate

local view = {}

local pageSize = 100
local additionMilliseconds = 1000
local levels = { "debug", "info", "warning", "error" }
local tones = { debug = "neutral", info = "information", warning = "warning", error = "danger" }

local reloadViewer
local addToViewer

-- The local time of an entry is asked for once, because filtering renders the same entries again on every keystroke.
local function row(entry)
    entry.presented = entry.presented or workpane.time.localPresentation(entry.timestamp_utc)

    return {
        id = tostring(entry.sequence),
        cells = {
            { text = entry.presented, monospace = true, muted = true },
            { text = text("logs.level." .. entry.level), tone = tones[entry.level] },
            { text = entry.source },
            { text = entry.category, muted = true },
            { text = entry.message },
        },
    }
end

-- Removes every stored entry after a destructive confirmation, and tells the reader when the table could not be emptied.
function view.clear()
    local confirmed = workpane.await(workpane.dialogs.confirm({
        title = translate("logs.viewer.clear-title"),
        message = translate("logs.viewer.clear-message"),
        detail = translate("logs.viewer.clear-detail"),
        confirmText = translate("logs.viewer.clear-action"),
        destructive = true,
    }))

    if confirmed and store.clear() ~= nil then
        workpane.notify.error(translate("logs.plugin.title"), translate("logs.error.clear-message"))
    end
end

-- Entries added join the top of the viewer, while an emptied log is read again from its start.
function view.refresh(kind)
    if reloadViewer ~= nil then
        workpane.task(kind == "added" and addToViewer or reloadViewer)
    end
end

-- The viewer is built once and stays mounted, so the table it shows is the only one to reload.
function view.build()
    local entries = {}
    local kept = pageSize
    local before = 0
    local loading = false
    local generation = 0
    local search = ""
    local level = "all"
    local levelOptions = { { value = "all", text = text("logs.viewer.all-levels") } }

    for index, name in ipairs(levels) do
        levelOptions[index + 1] = { value = name, text = text("logs.level." .. name) }
    end

    local listing
    local older
    local failureShown = false
    local empty = ui.emptyState({ text = text("logs.viewer.empty"), icon = "logs", grow = 1, visible = false })

    -- A log that cannot be read is told to the reader once until a read succeeds, and never written to the log this viewer reads.
    local function readFailed()
        if not failureShown then
            failureShown = true
            workpane.notify.error(translate("logs.plugin.title"), translate("logs.error.read-message"))
        end
    end

    local function render()
        local needle = search:lower()
        local rows = {}

        for _, entry in ipairs(entries) do
            entry.searchable = entry.searchable or (entry.source .. " " .. entry.category .. " " .. entry.message):lower()

            if (level == "all" or entry.level == level) and (needle == "" or entry.searchable:find(needle, 1, true) ~= nil) then
                rows[#rows + 1] = row(entry)
            end
        end

        listing:set({ rows = rows, visible = #rows > 0 })
        empty:set({ visible = #rows == 0 })
    end

    -- A page that arrives after a reload started over, or after new entries pushed the oldest out, would leave a gap, so it is ignored.
    local function loadPage()
        if loading then
            return
        end

        loading = true
        local current = generation
        local from = before
        local rows, failure = store.page(before, pageSize):await()

        if current ~= generation then
            return
        end

        loading = false

        if from ~= before then
            return
        end

        if failure ~= nil then
            readFailed()
            return
        end

        failureShown = false

        for _, stored in ipairs(rows) do
            entries[#entries + 1] = stored
        end

        kept = math.max(pageSize, #entries)

        if #rows > 0 then
            before = rows[#rows].sequence
        end

        older:set({ enabled = #rows == pageSize })
        render()
    end

    local function reload()
        generation = generation + 1
        entries = {}
        kept = pageSize
        before = 0
        loading = false
        older:set({ enabled = false })
        loadPage()
    end

    -- New entries join the top of the table at most once a second and push the oldest out past the pages the reader loaded, which the older button offers again, and more new entries than a page read the log again.
    local adding = false
    local addWanted = false

    local function addNewer()
        addWanted = true

        if adding then
            return
        end

        adding = true

        while addWanted do
            addWanted = false

            if entries[1] == nil then
                reload()
                break
            end

            local current = generation
            local rows, failure = store.since(entries[1].sequence, pageSize):await()

            if failure ~= nil then
                readFailed()
                break
            end

            if current ~= generation then
                break
            end

            failureShown = false

            if #rows == pageSize then
                reload()
                break
            end

            local merged = table.move(rows, 1, #rows, 1, {})
            table.move(entries, 1, math.min(#entries, kept - #rows), #rows + 1, merged)

            if #merged < #rows + #entries then
                before = merged[#merged].sequence
                older:set({ enabled = true })
            end

            entries = merged
            render()
            async.sleep(additionMilliseconds):await()
        end

        adding = false
    end

    -- Activating an entry shows its structured details, which the table has no room for.
    local function activated(event)
        for _, entry in ipairs(entries) do
            if tostring(entry.sequence) == event.id then
                workpane.await(workpane.dialogs.alert({ title = translate("logs.viewer.details-title"), message = entry.message, detail = entry.details_json }))
                return
            end
        end
    end

    local columns = {
        { id = "time", title = text("logs.viewer.time") },
        { id = "level", title = text("logs.viewer.level") },
        { id = "source", title = text("logs.viewer.source") },
        { id = "category", title = text("logs.viewer.category") },
        { id = "message", title = text("logs.viewer.message"), width = "stretch" },
    }

    listing = ui.table({ columns = columns, rows = {}, header = true, alternate = true, grow = 1, visible = false, onActivate = activated })
    older = ui.button({ text = text("logs.viewer.load-older"), enabled = false, onClick = loadPage })
    reloadViewer = reload
    addToViewer = addNewer
    workpane.task(reload)

    return ui.column({}, {
        ui.pageHeader({ title = text("logs.viewer.title") }, {
            ui.textField({ placeholder = text("logs.viewer.search"), clearButton = true, width = 240, onChange = function(event)
                search = event.value
                render()
            end }),
            ui.combo({ value = "all", options = levelOptions, width = 150, onChange = function(event)
                level = event.value
                render()
            end }),
            older,
            ui.button({ text = text("logs.viewer.refresh"), icon = "refresh", onClick = reload }),
            ui.button({ text = text("logs.viewer.clear"), icon = "clear", variant = "destructive", onClick = view.clear }),
        }),
        listing,
        empty,
    })
end

store.listen(view.refresh)

return view
