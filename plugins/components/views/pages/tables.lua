-- A table with a header, alternating rows, typed cells and row actions in a neutral, a toned and a destructive ink, whose rows can be removed through the destructive one.
local gallery = include("views/gallery")
local ui = workpane.ui
local text = workpane.i18n.text
local translate = workpane.i18n.translate

local columns = {
    { id = "name", title = text("components.tables.column-name"), width = "stretch" },
    { id = "kind", title = text("components.tables.column-kind") },
    { id = "status", title = text("components.tables.column-status"), align = "center" },
    { id = "size", title = text("components.tables.column-size"), align = "end" },
}

local function row(id, name, kind, status, tone, size)
    return {
        id = id,
        cells = {
            { text = name, icon = kind == "folder" and "folder" or "file" },
            { text = text("components.tables.kind-" .. kind), muted = true },
            { text = text("components.tables.status-" .. status), tone = tone },
            { text = size, monospace = true },
        },
        actions = {
            { id = "edit", icon = "edit", tooltip = text("components.tables.action-edit") },
            { id = "open", icon = "external-link", tooltip = text("components.tables.action-open"), tone = "success" },
            { id = "delete", icon = "clear", tooltip = text("components.tables.action-delete"), destructive = true },
        },
    }
end

return function(report)
    local rows = {
        row("bootstrap", "bootstrap.lua", "script", "ready", "success", "4.2 KB"),
        row("plugins", "plugins", "folder", "ready", "success", "—"),
        row("database", "workpane.sqlite3", "data", "busy", "warning", "812 KB"),
        row("unreadable", "workpane.sqlite3.unreadable", "data", "failed", "danger", "1.1 MB"),
        row("profile", "profile.png", "image", "ready", "success", "96 KB"),
    }

    local listing

    -- Removing a row asks first, then patches the rows the table shows.
    local function acted(event)
        report("action", event)

        if event.action ~= "delete" then
            return
        end

        local confirmed = workpane.await(workpane.dialogs.confirm({
            title = translate("components.tables.delete-title"),
            message = translate("components.tables.delete-message", event.id),
            confirmText = translate("components.tables.action-delete"),
            destructive = true,
        }))

        if not confirmed then
            return
        end

        local kept = {}

        for _, candidate in ipairs(rows) do
            if candidate.id ~= event.id then
                kept[#kept + 1] = candidate
            end
        end

        rows = kept
        listing:set({ rows = rows })
    end

    listing = ui.table({
        columns = columns,
        rows = rows,
        header = true,
        alternate = true,
        selected = "bootstrap",
        height = 240,
        onSelect = gallery.reporter(report, "select"),
        onActivate = gallery.reporter(report, "activate"),
        onAction = acted,
    })

    -- The subtle selection keeps the tones of a selected row, which a list of states reads better with.
    local subtle = ui.table({ columns = columns, rows = { rows[1], rows[3], rows[4] }, header = true, selection = "subtle", selected = "database", height = 150, onSelect = gallery.reporter(report, "select") })

    return {
        gallery.section("components.tables.files", { listing }),
        gallery.section("components.tables.subtle", { subtle }),
    }
end
