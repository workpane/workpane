-- Discovers the plugins of every plugin folder, loads each one in an environment of its own and orders them by their dependencies.
local api = require("workpane.api")
local async = require("async")
local bridge = require("workpane.bridge")
local lifecycle = require("workpane.lifecycle")
local fs = require("fs")
local plain = require("workpane.plain")
local task = require("workpane.task")

local loader = {}

-- A plugin reaches the Varn modules through require and never the modules of the SDK, which hold the host bridge.
local allowedModules = { async = true, crypto = true, datetime = true, fs = true, http = true, json = true, platform = true, process = true, socket = true, xml = true, zip = true }

-- The metatable of strings is shared by every plugin, so it answers a name instead of itself and no plugin can change the methods of a string.
getmetatable("").__metatable = "string"

local function copied(library)
    local copy = {}

    for name, value in pairs(library) do
        copy[name] = value
    end

    return copy
end

-- A module is copied whole with the tables inside it, so a plugin that replaces one of its functions changes only its own copy.
local function copiedDeep(value, seen)
    if type(value) ~= "table" then
        return value
    end

    if seen[value] ~= nil then
        return seen[value]
    end

    local copy = {}
    seen[value] = copy

    for name, item in pairs(value) do
        copy[name] = copiedDeep(item, seen)
    end

    return copy
end

-- A plugin reads only the metatables it set itself, so the metatables of the SDK, of the Varn objects and of other plugins stay out of its reach.
-- The mark the JSON reader puts on the tables it reads reads as none, since such a table is plain data to every plugin.
local function metatables()
    local owned = setmetatable({}, { __mode = "k" })

    local function ownGetmetatable(value)
        local metatable = getmetatable(value)

        if plain.marker(metatable) then
            return nil
        end

        if type(metatable) ~= "table" or owned[metatable] then
            return metatable
        end

        return "workpane.protected"
    end

    local function ownSetmetatable(target, metatable)
        if type(metatable) == "table" then
            owned[metatable] = true
        end

        return setmetatable(target, metatable)
    end

    return ownGetmetatable, ownSetmetatable
end

-- Every plugin receives its own copies of the standard library tables, so no plugin can replace a function another plugin calls.
local function standardLibrary()
    local ownGetmetatable, ownSetmetatable = metatables()

    return {
        assert = assert,
        coroutine = copied(coroutine),
        error = error,
        getmetatable = ownGetmetatable,
        ipairs = ipairs,
        math = copied(math),
        next = next,
        os = { clock = os.clock, date = os.date, time = os.time },
        pairs = pairs,
        pcall = pcall,
        rawequal = rawequal,
        rawget = rawget,
        rawlen = rawlen,
        rawset = rawset,
        select = select,
        setmetatable = ownSetmetatable,
        string = copied(string),
        table = copied(table),
        tonumber = tonumber,
        tostring = tostring,
        type = type,
        utf8 = copied(utf8),
        xpcall = xpcall,
    }
end

-- Every Lua file of a plugin is read before anything of it runs, so its own modules load from memory without waiting on the disk.
local function readSources(directory, relative, sources)
    local folder = relative == "" and directory or (directory .. "/" .. relative)
    local names = task.await(fs.readdir(folder))
    table.sort(names)

    for _, name in ipairs(names) do
        local path = folder .. "/" .. name
        local entry = relative == "" and name or (relative .. "/" .. name)
        local stat = task.await(fs.stat(path))

        if stat.isDir and name ~= "assets" then
            readSources(directory, entry, sources)
        elseif stat.isFile and name:sub(-4) == ".lua" then
            sources[entry] = task.await(fs.readFile(path))
        end
    end

    return sources
end

-- A plugin starts its coroutines through the SDK, so each one keeps the frame loop polling Lua and reports its failure under the plugin, and it never stops the loop.
-- The handler of failures nobody receives serves the whole runtime and belongs to the SDK, so no plugin replaces or clears it for the others.
local function asyncFor(owner)
    local module = copiedDeep(async, {})

    module.run = nil
    module.onFailure = nil
    module.spawn = function(work)
        lifecycle.check(owner)

        if type(work) ~= "function" then
            bridge.raise("task_work_invalid", "A task runs a function", owner)
        end

        task.run(owner, "async", work)
    end

    return module
end

local function environment(workpane, sources, directory)
    local env
    local loaded = {}
    local modules = { async = asyncFor(workpane.plugin.id) }

    -- Loads another file of the same plugin once, in the environment of that plugin, and answers what it returned.
    local function include(name)
        local file = name .. ".lua"

        if loaded[file] ~= nil then
            return loaded[file]
        end

        local source = sources[file]

        if source == nil then
            error(bridge.failure({ code = "plugin_module_missing", message = "A plugin includes a file it does not carry", detail = file }), 2)
        end

        local chunk, failure = load(source, "@" .. directory .. "/" .. file, "t", env)

        if chunk == nil then
            error(bridge.failure({ code = "plugin_module_invalid", message = "A plugin file is not valid Lua", detail = failure }), 2)
        end

        local value = chunk()
        loaded[file] = value == nil and true or value

        return loaded[file]
    end

    local function guardedRequire(name)
        if not allowedModules[name] then
            error(bridge.failure({ code = "plugin_module_refused", message = "A plugin requires a module it may not reach", detail = tostring(name) }), 2)
        end

        if modules[name] == nil then
            modules[name] = copiedDeep(require(name), {})
        end

        return modules[name]
    end

    local function print(...)
        local values = table.pack(...)
        local parts = {}

        for index = 1, values.n do
            parts[index] = tostring(values[index])
        end

        local text = table.concat(parts, " ")

        if text ~= "" then
            workpane.log.info("print", text)
        end
    end

    env = setmetatable({ workpane = workpane, include = include, require = guardedRequire, print = print }, { __index = standardLibrary() })
    env._G = env

    return env
end

-- A folder name becomes the identity of everything its plugin does, so it follows the grammar of an identifier and never takes the name of the core.
local function validIdentifier(name)
    return name ~= "workpane" and name:find("^[a-z0-9%-]+$") ~= nil and name:find("^%-") == nil and name:find("%-$") == nil and name:find("--", 1, true) == nil
end

-- Migrations are a list of lists of statements, which the manager applies in order before the plugin starts.
local function validMigrations(migrations)
    if migrations == nil then
        return true
    end

    if type(migrations) ~= "table" then
        return false
    end

    for index, statements in pairs(migrations) do
        if math.type(index) ~= "integer" or index < 1 or index > #migrations or type(statements) ~= "table" then
            return false
        end

        for position, statement in pairs(statements) do
            if math.type(position) ~= "integer" or type(statement) ~= "string" then
                return false
            end
        end
    end

    return true
end

local function validDependencies(dependencies)
    if dependencies == nil then
        return true
    end

    if type(dependencies) ~= "table" then
        return false
    end

    for key, dependency in pairs(dependencies) do
        if math.type(key) ~= "integer" or type(dependency) ~= "string" then
            return false
        end
    end

    return true
end

-- Reads a plugin from its folder into an environment of its own and answers its definition and catalog, which is also how a plugin enabled again sees the code on disk.
function loader.load(name, directory, info)
    local sources = readSources(directory, "", {})
    local workpane = api.create(name, directory, info)
    local env = environment(workpane, sources, directory)
    local translations = env.include("translations")
    local definition = env.include("plugin")

    if type(definition) ~= "table" or definition.id ~= name then
        error(bridge.failure({ code = "plugin_identifier_mismatch", message = "A plugin declares an identifier other than the directory named after it", detail = name }), 0)
    end

    if definition.sdk ~= api.level then
        error(bridge.failure({ code = "plugin_sdk_unsupported", message = "A plugin declares a level of the SDK other than the one the product offers", detail = name .. ".sdk" }), 0)
    end

    for _, field in ipairs({ "enabled", "start", "stop" }) do
        if definition[field] ~= nil and type(definition[field]) ~= "function" then
            error(bridge.failure({ code = "plugin_definition_invalid", message = "A plugin declares a hook that is not a function", detail = name .. "." .. field }), 0)
        end
    end

    if definition.offByDefault ~= nil and type(definition.offByDefault) ~= "boolean" then
        error(bridge.failure({ code = "plugin_definition_invalid", message = "A plugin declares whether it is off by default with something other than a boolean", detail = name .. ".offByDefault" }), 0)
    end

    if not validDependencies(definition.dependencies) then
        error(bridge.failure({ code = "plugin_definition_invalid", message = "A plugin declares its dependencies as something other than a list of identifiers", detail = name .. ".dependencies" }), 0)
    end

    if not validMigrations(definition.migrations) then
        error(bridge.failure({ code = "plugin_definition_invalid", message = "A plugin declares its migrations as something other than lists of statements", detail = name .. ".migrations" }), 0)
    end

    return { id = name, directory = directory, definition = definition, translations = translations }
end

-- Answers every plugin found in the plugin folders, the bundled one first, each loaded or with the failure that kept it from loading, and every folder or entry that was refused.
-- A plugin whose name an earlier folder already answered is refused, so a folder never replaces a plugin the product carries.
function loader.discover(folders, info)
    local found = {}
    local refused = {}
    local seen = {}

    local function consider(name, directory, bundled)
        if seen[name] ~= nil then
            refused[#refused + 1] = { id = name, failure = bridge.failure({ code = "plugin_duplicate", message = "A plugin with the same name was already loaded from another folder", detail = directory }) }
            return
        end

        seen[name] = directory

        if not validIdentifier(name) then
            refused[#refused + 1] = { id = name, failure = bridge.failure({ code = "plugin_identifier_invalid", message = "A plugin folder is named with lowercase letters, numbers and single hyphens, and never after the core", detail = directory }) }
            return
        end

        local loaded, candidate = pcall(loader.load, name, directory, info)
        found[#found + 1] = { id = name, directory = directory, bundled = bundled, candidate = loaded and candidate or nil, failure = not loaded and bridge.failure(candidate) or nil }
    end

    for index, folder in ipairs(folders) do
        local listed, names = pcall(task.await, fs.readdir(folder))

        if not listed then
            refused[#refused + 1] = { folder = folder, failure = bridge.failure({ code = "plugin_folder_unreadable", message = "A plugin folder cannot be read", detail = folder }) }
            names = {}
        end

        table.sort(names)

        -- An entry that cannot even be described, such as a link to nothing, is refused by its name instead of stopping every plugin after it.
        for _, name in ipairs(names) do
            local directory = folder .. "/" .. name
            local described, entry = pcall(task.await, fs.stat(directory))

            if not described then
                refused[#refused + 1] = { id = name, failure = bridge.failure({ code = "plugin_unreadable", message = "A plugin folder entry cannot be read", detail = directory }) }
            elseif entry.isDir then
                consider(name, directory, index == 1)
            end
        end
    end

    return found, refused
end

-- The contributions of a plugin as the host validates them, without the functions that stay in Lua and without the catalog, which was installed when the plugin was discovered.
function loader.manifest(candidate)
    local definition = candidate.definition
    local navigation = {}
    local bands = {}
    local settings = {}

    for index, item in ipairs(definition.navigation or {}) do
        local shortcuts = {}

        for position, shortcut in ipairs(item.shortcuts or {}) do
            shortcuts[position] = { id = shortcut.id, keys = shortcut.keys }
        end

        navigation[index] = { id = item.id, titleKey = item.titleKey, icon = item.icon, placement = item.placement, order = item.order, preload = item.preload == true, shortcuts = shortcuts }
    end

    for index, band in ipairs(definition.bands or {}) do
        bands[index] = { id = band.id, titleKey = band.titleKey, placement = band.placement, order = band.order, height = band.height }
    end

    for index, group in ipairs(definition.settings or {}) do
        local sections = {}

        for sectionIndex, section in ipairs(group.sections or {}) do
            sections[sectionIndex] = { id = section.id, titleKey = section.titleKey, searchKeys = section.searchKeys }
        end

        settings[index] = { id = group.id, titleKey = group.titleKey, sections = sections }
    end

    return { id = candidate.id, titleKey = definition.titleKey, descriptionKey = definition.descriptionKey, directory = candidate.directory, dependencies = definition.dependencies or {}, navigation = navigation, bands = bands, settings = settings }
end

return loader
