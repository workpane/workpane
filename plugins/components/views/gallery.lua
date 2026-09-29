-- The pieces every gallery page shares: the page frame, its titled sections, the labeled samples and the line naming the last event.
local ui = workpane.ui
local text = workpane.i18n.text

local gallery = {}

-- Writes an event value as sorted pairs, so the line under a page stays readable whatever a component reported.
local function describe(value)
    if type(value) ~= "table" then
        return tostring(value)
    end

    local keys = {}

    for key in pairs(value) do
        keys[#keys + 1] = key
    end

    table.sort(keys, function(first, second)
        return tostring(first) < tostring(second)
    end)

    local parts = {}

    for index, key in ipairs(keys) do
        parts[index] = tostring(key) .. "=" .. describe(value[key])
    end

    return "{" .. table.concat(parts, ", ") .. "}"
end

-- A page scrolls its sections under the description of the category, and the line naming the last event stays below them.
function gallery.page(descriptionKey, sections, events)
    local children = { ui.label({ text = text(descriptionKey), style = "muted" }) }

    for _, section in ipairs(sections) do
        children[#children + 1] = section
    end

    return ui.column({ grow = 1 }, {
        ui.scroll({ grow = 1 }, ui.column({ padding = { 20, 28, 28, 28 }, spacing = 22 }, children)),
        ui.row({ padding = { 8, 28, 8, 28 }, borders = { "top" } }, { events }),
    })
end

function gallery.section(titleKey, children)
    return ui.column({ spacing = 10 }, {
        ui.sectionTitle({ text = text(titleKey) }),
        ui.card({ padding = 16, spacing = 14 }, children),
    })
end

-- A sample names what it shows on the left and keeps the component at its natural size on the right.
function gallery.sample(labelKey, component)
    return ui.row({ spacing = 16 }, {
        ui.label({ text = text(labelKey), style = "muted", width = 200, wrap = false, align = "center" }),
        component,
    })
end

function gallery.events()
    local line = ui.label({ text = text("components.events.none"), style = "monospace", color = "text-muted", wrap = false })

    local function report(name, value)
        line:set({ text = text("components.events.last", name, describe(value or {})) })
    end

    return line, report
end

-- Answers a handler that reports its event under the given name, which is what most samples need.
function gallery.reporter(report, name)
    return function(value)
        report(name, value)
    end
end

return gallery
