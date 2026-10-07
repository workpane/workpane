-- Finds a file of an open folder by the characters of its path, ranking file names and unbroken runs first.
local catalog = include("catalog")
local paths = include("paths")

local ui = workpane.ui
local text = workpane.i18n.text
local number = workpane.i18n.number
local translate = workpane.i18n.translate

local finder = {}

-- Every character of the query must appear in order, each adds one more than the run of adjacent matches before it, and a match ending in the file name counts twice.
function finder.score(path, query)
    if query == "" then
        return 0
    end

    local lowered = path:lower()
    local wanted = query:lower()
    local score = 0
    local run = 0
    local previous = 0

    for index = 1, #wanted do
        local found = lowered:find(wanted:sub(index, index), previous + 1, true)

        if found == nil then
            return nil
        end

        run = found == previous + 1 and index > 1 and run + 1 or 0
        score = score + 1 + run
        previous = found
    end

    local lastSeparator = lowered:match("^.*()/") or 0
    return previous > lastSeparator and score * 2 or score
end

-- The files are ordered by score and then by the order of the walk, and only the first rows are listed.
function finder.rank(files, query, maximum)
    local ranked = {}

    for index, path in ipairs(files) do
        local score = finder.score(path, query)

        if score ~= nil then
            ranked[#ranked + 1] = { path = path, score = score, index = index }
        end
    end

    table.sort(ranked, function(first, second)
        if first.score ~= second.score then
            return first.score > second.score
        end

        return first.index < second.index
    end)

    local rows = {}

    for index = 1, math.min(#ranked, maximum) do
        rows[index] = ranked[index].path
    end

    return rows
end

-- The dialog lists the files of the folder as soon as the walk answers, the arrows move the selection from the query, and Enter or a double click opens the selected one.
function finder.open(root, opened)
    local files = {}
    local rows = {}
    local query = ""
    local chosen
    local dialog
    local summary = ui.label({ text = "", style = "caption", color = "text-muted", wrap = false })
    local list = ui.list({ items = {}, onSelect = function(event)
        chosen = event.id
    end, onActivate = function(event)
        chosen = event.id
        dialog:close("open")
    end })

    local function render()
        rows = finder.rank(files, query, catalog.limit("maximumReferences"))
        local items = {}

        for index, path in ipairs(rows) do
            items[index] = { id = path, text = path:match("[^/]+$"), detail = path }
        end

        chosen = rows[1]
        list:set({ items = items, selected = chosen or "" })
    end

    local field = ui.textField({ placeholder = text("code-editor.finder.placeholder"), clearButton = true, arrows = true, onChange = function(event)
        query = event.value:match("^%s*(.-)%s*$")
        render()
    end, onArrow = function(event)
        local position = 0

        for index, path in ipairs(rows) do
            position = path == chosen and index or position
        end

        chosen = rows[math.max(1, math.min(#rows, position + (event.direction == "up" and -1 or 1)))]

        if chosen ~= nil then
            list:set({ selected = chosen })
            list:command("reveal", { id = chosen })
        end
    end, onSubmit = function()
        if chosen ~= nil then
            dialog:close("open")
        end
    end })

    workpane.task(function()
        local walked = workpane.await(workpane.files.walk(root, { maximum = catalog.limit("maximumWorkspaceFiles"), skip = { ".git" } }))
        files = walked.paths
        summary:set({ text = text(walked.complete and "code-editor.finder.count" or "code-editor.finder.count-capped", number(#files, 0)) })
        render()
    end)

    dialog = workpane.dialogs.custom({ title = translate("code-editor.finder.title"), width = 560, content = ui.column({ spacing = 10 }, { field, ui.scroll({ height = 320 }, list), summary }) })
    field:command("focus")

    if dialog:await() == "open" and chosen ~= nil then
        opened(paths.join(root, chosen))
    end
end

return finder
