-- The code editor with syntax colors, driven by a language choice and switches for its options, and a language the page defines itself with markers, highlights, proposals and hover texts.
local gallery = include("views/gallery")
local ui = workpane.ui
local text = workpane.i18n.text

local samples = {
    lua = 'local plugin = {}\n\n-- Answers a greeting for the given name.\nfunction plugin.greet(name)\n    return string.format("Hello, %s!", name)\nend\n\nreturn plugin\n',
    cpp = '#include <iostream>\n\n// Prints a greeting for the given name.\nint main() {\n    const std::string name = "Workpane";\n    std::cout << "Hello, " << name << "!\\n";\n    return 0;\n}\n',
    python = '# Answers a greeting for the given name.\ndef greet(name: str) -> str:\n    return f"Hello, {name}!"\n\n\nprint(greet("Workpane"))\n',
    json = '{\n    "id": "components",\n    "debug": true,\n    "order": 100,\n    "tags": ["gallery", "debug"]\n}\n',
    sql = 'SELECT level, COUNT(*) AS total\nFROM logs__entries\nWHERE created_at >= :since\nGROUP BY level\nORDER BY total DESC;\n',
    markdown = '# Workpane\n\nA **modular** workspace with *plugins* written in Lua.\n\n- Components\n- Logs\n- Donate\n',
}

local languages = {
    { value = "lua", text = "Lua" },
    { value = "cpp", text = "C++" },
    { value = "python", text = "Python" },
    { value = "json", text = "JSON" },
    { value = "sql", text = "SQL" },
    { value = "markdown", text = "Markdown" },
}

-- A small language the page defines the way a plugin reading a catalog would, with the words it proposes while the reader types.
local tasks = {
    name = "Tasks",
    lineComment = "#",
    doubleQuotes = true,
    escape = "\\",
    keywords = { "task", "when", "every", "run", "done" },
    declarations = { "daily", "weekly", "hourly" },
}

local function definedEditor(report)
    local editor

    local function proposals(event)
        local found = {}

        for _, word in ipairs({ "task", "when", "every", "run", "done", "daily", "weekly", "hourly" }) do
            if word:sub(1, #event.word) == event.word then
                found[#found + 1] = { label = word, insert = word }
            end
        end

        editor:command("suggest", { request = event.request, items = found })
        report("complete-request", event)
    end

    local function hovered(event)
        editor:command("hover-text", { line = event.line, column = event.column, text = workpane.i18n.translate("components.code-editor.hover-text", event.word) })
        report("hover", event)
    end

    editor = ui.codeEditor({
        value = '# Tasks the plugin schedules\ntask "backup" every daily\n    run "copy"\ndone\n',
        language = tasks,
        height = 180,
        completion = true,
        hovers = true,
        markers = {
            { line = 2, column = 21, endColumn = 26, tone = "information", message = workpane.i18n.translate("components.code-editor.range-marker"), detail = "tasks(schedule)", related = { { place = "tasks:2:6", message = workpane.i18n.translate("components.code-editor.related") } } },
            { line = 3, tone = "warning", message = workpane.i18n.translate("components.code-editor.marker") },
        },
        highlights = { { line = 2, column = 6, length = 8, role = "string" } },
        onCompleteRequest = proposals,
        onHover = hovered,
        onCursor = gallery.reporter(report, "cursor"),
    })

    return editor
end

return function(report)
    local editor = ui.codeEditor({ value = samples.lua, language = "lua", height = 320, onChange = gallery.reporter(report, "change"), onZoom = gallery.reporter(report, "zoom") })

    local function chosen(event)
        editor:set({ language = event.value, value = samples[event.value] })
        report("change", event)
    end

    local function option(key, labelKey, checked)
        return ui.checkbox({ text = text(labelKey), checked = checked, onChange = function(event)
            editor:set({ [key] = event.checked })
            report("change", { [key] = event.checked })
        end })
    end

    return {
        gallery.section("components.code-editor.editor", {
            ui.row({ spacing = 16 }, {
                ui.combo({ value = "lua", options = languages, width = 180, onChange = chosen }),
                option("lineNumbers", "components.code-editor.line-numbers", true),
                option("wordWrap", "components.code-editor.word-wrap", false),
                option("whitespace", "components.code-editor.whitespace", false),
                option("minimap", "components.code-editor.minimap", false),
                option("readOnly", "components.code-editor.read-only", false),
            }),
            editor,
            ui.label({ text = text("components.code-editor.zoom-hint"), style = "muted" }),
        }),
        gallery.section("components.code-editor.defined", {
            definedEditor(report),
            ui.label({ text = text("components.code-editor.defined-hint"), style = "muted" }),
        }),
    }
end
