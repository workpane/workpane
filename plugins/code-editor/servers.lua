-- Finds the executable of every language server the catalog names, first candidate first, and keeps what it found until the reader asks to look again.
local catalog = include("catalog")

local servers = {}

local resolved = {}
local searching = false
local listeners = {}

-- A program started from the desktop may not inherit the search path of a shell, so the folders where package managers install servers are searched too.
local function directories()
    local home = workpane.system.home()
    local list = { "/opt/homebrew/bin", "/usr/local/bin", "/usr/bin" }

    for _, relative in ipairs({ ".local/bin", ".cargo/bin", "go/bin", ".npm-global/bin", ".bun/bin", ".deno/bin" }) do
        list[#list + 1] = home .. "/" .. relative
    end

    return list
end

function servers.listen(handler)
    listeners[#listeners + 1] = handler
end

local function announce()
    for _, listener in ipairs(listeners) do
        listener()
    end
end

-- A candidate the search cannot look up counts as not found, so one refused lookup never leaves the search running.
function servers.refresh()
    if searching then
        return
    end

    searching = true
    announce()

    workpane.task(function()
        local found = {}
        local searched = directories()

        for _, entry in ipairs(catalog.servers()) do
            for _, candidate in ipairs(entry.candidates) do
                local path = found[entry.language] == nil and workpane.process.find(candidate.executable, searched):await() or nil

                if path ~= nil then
                    found[entry.language] = { path = path, arguments = candidate.arguments or {} }
                end
            end
        end

        resolved = found
        searching = false
        announce()
    end)
end

function servers.searching()
    return searching
end

function servers.executable(language)
    return resolved[language]
end

-- Answers whether the search found the executable of at least one language server.
function servers.any()
    return next(resolved) ~= nil
end

return servers
