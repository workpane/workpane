-- The seventeen arrangements of a workspace, from one terminal to twelve, and the moves that keep every terminal either in a slot or on the shelf.
local layouts = {}

local presets = {
    { id = "1-single", slots = 1, columns = 1, rows = 1 },
    { id = "2-columns", slots = 2, columns = 2, rows = 1 },
    { id = "2-rows", slots = 2, columns = 1, rows = 2 },
    { id = "3-left", slots = 3, columns = 2, rows = 2 },
    { id = "3-bottom", slots = 3, columns = 2, rows = 2 },
    { id = "4-grid", slots = 4, columns = 2, rows = 2 },
    { id = "5-balanced", slots = 5, columns = 3, rows = 2 },
    { id = "6-columns", slots = 6, columns = 3, rows = 2 },
    { id = "6-rows", slots = 6, columns = 2, rows = 3 },
    { id = "7-balanced", slots = 7, columns = 3, rows = 3 },
    { id = "8-columns", slots = 8, columns = 4, rows = 2 },
    { id = "8-rows", slots = 8, columns = 2, rows = 4 },
    { id = "9-grid", slots = 9, columns = 3, rows = 3 },
    { id = "10-balanced", slots = 10, columns = 5, rows = 2 },
    { id = "11-balanced", slots = 11, columns = 4, rows = 3 },
    { id = "12-columns", slots = 12, columns = 4, rows = 3 },
    { id = "12-rows", slots = 12, columns = 3, rows = 4 },
}

function layouts.presets()
    return presets
end

function layouts.preset(id)
    for _, preset in ipairs(presets) do
        if preset.id == id then
            return preset
        end
    end

    return nil
end

-- The cells of a preset from zero, in slot order, where the two presets with a large slot let it span two cells.
function layouts.cells(preset)
    if preset.id == "3-left" then
        return { { column = 0, row = 0, rowSpan = 2 }, { column = 1, row = 0 }, { column = 1, row = 1 } }
    end

    if preset.id == "3-bottom" then
        return { { column = 0, row = 0 }, { column = 1, row = 0 }, { column = 0, row = 1, columnSpan = 2 } }
    end

    local cells = {}

    for index = 0, preset.slots - 1 do
        cells[index + 1] = { column = index % preset.columns, row = index // preset.columns }
    end

    return cells
end

-- The slot showing a terminal, or nil when the terminal is on the shelf or elsewhere.
function layouts.slotOf(tab, id)
    for index, assigned in ipairs(tab.slots) do
        if assigned == id then
            return index
        end
    end

    return nil
end

function layouts.contains(tab, id)
    if layouts.slotOf(tab, id) ~= nil then
        return true
    end

    for _, shelved in ipairs(tab.shelf) do
        if shelved == id then
            return true
        end
    end

    return false
end

function layouts.firstEmpty(tab)
    for index, assigned in ipairs(tab.slots) do
        if assigned == "" then
            return index
        end
    end

    return nil
end

-- Takes a terminal out of its slot or off the shelf, leaving the slot empty.
function layouts.remove(tab, id)
    for index, assigned in ipairs(tab.slots) do
        if assigned == id then
            tab.slots[index] = ""
        end
    end

    for index = #tab.shelf, 1, -1 do
        if tab.shelf[index] == id then
            table.remove(tab.shelf, index)
        end
    end
end

-- A smaller preset moves the terminals of the slots it loses to the shelf, so changing the layout never closes a terminal.
function layouts.change(tab, preset)
    for index = preset.slots + 1, #tab.slots do
        if tab.slots[index] ~= "" then
            tab.shelf[#tab.shelf + 1] = tab.slots[index]
        end

        tab.slots[index] = nil
    end

    for index = #tab.slots + 1, preset.slots do
        tab.slots[index] = ""
    end

    tab.preset = preset.id
end

-- A terminal placed in a slot trades places with the terminal there, which goes where the placed one came from or to the front of the shelf.
function layouts.assign(tab, id, slot)
    local from = layouts.slotOf(tab, id)
    local displaced = tab.slots[slot]

    layouts.remove(tab, id)
    tab.slots[slot] = id

    if displaced == "" or displaced == id then
        return
    end

    if from ~= nil then
        tab.slots[from] = displaced
        return
    end

    table.insert(tab.shelf, 1, displaced)
end

function layouts.shelve(tab, id)
    layouts.remove(tab, id)
    tab.shelf[#tab.shelf + 1] = id
end

-- The focused terminal is always one on screen, and a workspace with nothing on screen focuses nothing.
function layouts.normalizeFocus(tab)
    if tab.focused ~= "" and layouts.slotOf(tab, tab.focused) ~= nil then
        return
    end

    tab.focused = ""

    for _, assigned in ipairs(tab.slots) do
        if assigned ~= "" then
            tab.focused = assigned
            return
        end
    end
end

return layouts
