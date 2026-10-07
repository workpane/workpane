-- The plugins the product discovered, offered to every plugin through the application without reaching the manager that starts them.
local directory = {}

local lister = function()
    return {}
end

function directory.install(list)
    lister = list
end

-- Answers each discovered plugin with its identifier, its catalog keys, whether it is bundled and its state, as copies of plain data.
function directory.list()
    local listed = {}

    for index, plugin in ipairs(lister()) do
        listed[index] = { id = plugin.id, titleKey = plugin.titleKey, descriptionKey = plugin.descriptionKey, bundled = plugin.bundled, state = plugin.state }
    end

    return listed
end

return directory
