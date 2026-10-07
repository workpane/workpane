-- The tools an agent may call: files inside the working directory, commands, the web, generated media, skills, the task itself and what the connected servers publish.
local async = require("async")
local crypto = require("crypto")
local fs = require("fs")
local http = require("http")
local catalog = include("catalog")
local codec = include("codec")
local commands = include("commands")
local connections = include("connections")
local cron = include("cron")
local preferences = include("preferences")
local protocols = include("protocols")
local resources = include("resources")
local spaces = include("spaces")
local store = include("store")

local tools = {}

local readableArgument = 56
local maximumReadBytes = 1 << 20
local maximumListed = 200
local defaultCommandTimeout = 120
local maximumFetchBytes = 1 << 19
local fetchTimeoutSeconds = 30
local resultHead = 18000
local resultTail = 6000
local maximumViewedImageBytes = 4 * 1024 * 1024
local maximumSkillBytes = 1 << 18
local maximumMatches = 200
local maximumSearchedFileBytes = 1 << 20
local recentRuns = 5
local mediaTimeoutSeconds = 180
local searchTimeoutSeconds = 30
local defaultSearchResults = 5
local maximumSearchResults = 20
local maximumMediaBytes = 1 << 24
local maximumWalked = 100000
local maximumSearched = 5000

local imageTypes = { png = "image/png", jpg = "image/jpeg", jpeg = "image/jpeg", webp = "image/webp", gif = "image/gif" }
local reading = { read_file = true, read_image = true, list_directory = true, describe_path = true, search_files = true, search_text = true }
local writing = { write_file = true, edit_file = true, create_directory = true, remove_path = true, generate_image = true, generate_speech = true }
local pairsOfPaths = { move_path = true, copy_file = true }
local detached = { fetch_url = true, web_search = true, list_voices = true, describe_task = true, list_tasks = true, read_task = true, list_skills = true, search_skills = true, read_skill = true, read_skill_file = true, list_agent_plugins = true, read_agent_plugin_file = true, list_workpane_plugins = true, list_workpane_capabilities = true, list_mcp_resources = true, read_mcp_resource = true, list_mcp_prompts = true, read_mcp_prompt = true }
local searchSkipped = { ".git", "node_modules", ".build", "build", "dist", "target", ".venv", "vendor", "Pods", ".gradle", ".dart_tool" }

local running = {}

local function property(kind, description)
    return { type = kind, description = description }
end

local function schema(name, properties, required, activityArgument)
    local key = name:gsub("_", "-")
    local parameters = { type = "object", properties = properties }

    if required ~= nil then
        parameters.required = codec.list(required)
    end

    return { name = name, descriptionKey = "ai.tool." .. key, parameters = parameters, titleKey = "ai.tool-title." .. key, activityKey = "ai.tool-activity." .. key, activityArgument = activityArgument }
end

local native = {
    schema("read_file", { path = property("string", "Path inside the task working directory, written relative to it or in full"), start_line = property("integer", "First line to read, counting from one"), end_line = property("integer", "Last line to read, counting from one") }, { "path" }, "path"),
    schema("write_file", { path = property("string", "Path inside the task working directory, written relative to it or in full"), content = property("string", "Complete text content to write") }, { "path", "content" }, "path"),
    schema("edit_file", { path = property("string", "Path inside the task working directory, written relative to it or in full"), old_text = property("string", "Exact text to replace, including its indentation"), new_text = property("string", "Text that replaces it, empty to delete the passage"), replace_all = property("boolean", "Replace every occurrence instead of requiring exactly one") }, { "path", "old_text", "new_text" }, "path"),
    schema("read_image", { path = property("string", "Image file relative to the task working directory") }, { "path" }, "path"),
    schema("list_directory", { path = property("string", "Directory inside the task working directory, written relative to it or in full") }, nil, "path"),
    schema("create_directory", { path = property("string", "Directory to create, relative to the task working directory") }, { "path" }, "path"),
    schema("move_path", { source = property("string", "Path to move or rename"), destination = property("string", "Destination path, which must not exist") }, { "source", "destination" }, "source"),
    schema("copy_file", { source = property("string", "File to copy"), destination = property("string", "Destination path, which must not exist") }, { "source", "destination" }, "source"),
    schema("remove_path", { path = property("string", "File or directory to remove"), recursive = property("boolean", "Remove a directory together with everything inside it") }, { "path" }, "path"),
    schema("describe_path", { path = property("string", "Path to inspect") }, { "path" }, "path"),
    schema("search_files", { pattern = property("string", "Name pattern such as \"*.cpp\""), path = property("string", "Directory to search, defaulting to the working directory"), contains = property("string", "Text every matching file must contain") }, { "pattern" }, "pattern"),
    schema("search_text", { text = property("string", "Text to find, compared without regard to the case of ASCII letters"), path = property("string", "Directory to search, defaulting to the working directory") }, { "text" }, "text"),
    schema("run_command", { command = property("string", "Command to run inside the task working directory"), timeout_seconds = property("integer", "Time limit in seconds, where zero means unlimited") }, { "command" }, "command"),
    schema("fetch_url", { url = property("string", "Absolute HTTP or HTTPS address to fetch") }, { "url" }, "url"),
    schema("web_search", { query = property("string", "Words to search the web for"), count = property("integer", "How many results to return, up to twenty") }, { "query" }, "query"),
    schema("generate_image", { prompt = property("string", "Description of the image to generate"), path = property("string", "Destination file inside the task working directory"), size = property("string", "Requested size such as \"1024x1024\"") }, { "prompt", "path" }, "prompt"),
    schema("generate_speech", { text = property("string", "Text to speak"), path = property("string", "Destination file inside the task working directory"), voice = property("string", "Voice to speak with, defaulting to the configured one") }, { "text", "path" }, "text"),
    schema("list_voices", {}, nil, nil),
    schema("list_skills", {}, nil, nil),
    schema("search_skills", { query = property("string", "Words to match against the skill name and description") }, { "query" }, "query"),
    schema("read_skill", { name = property("string", "Name of the skill to load") }, { "name" }, "name"),
    schema("read_skill_file", { name = property("string", "Name of the skill that carries the file"), path = property("string", "Path of the file inside the skill, such as \"reference.md\"") }, { "name", "path" }, "path"),
    schema("list_agent_plugins", {}, nil, nil),
    schema("read_agent_plugin_file", { plugin = property("string", "Name of the agent plugin"), path = property("string", "Path of the file inside the plugin, such as \"commands/review.md\"") }, { "plugin", "path" }, "path"),
    schema("describe_task", {}, nil, nil),
    schema("list_tasks", {}, nil, nil),
    schema("read_task", { id = property("string", "Identifier of a task of the board") }, { "id" }, "id"),
    schema("list_workpane_plugins", {}, nil, nil),
    schema("list_workpane_capabilities", {}, nil, nil),
    schema("request_workpane_capability", { name = property("string", "Name of the capability, such as \"workspace.page.open\""), payload = property("object", "Payload that follows the schema the capability declares") }, { "name", "payload" }, "name"),
}

local serverHelpers = {
    schema("list_mcp_resources", {}, nil, nil),
    schema("read_mcp_resource", { server = property("string", "Identifier of the server that publishes the resource"), uri = property("string", "Address of the resource to read") }, { "server", "uri" }, "uri"),
    schema("list_mcp_prompts", {}, nil, nil),
    schema("read_mcp_prompt", { server = property("string", "Identifier of the server that publishes the prompt"), name = property("string", "Name of the prompt to load"), arguments = property("object", "Arguments the prompt declares") }, { "server", "name" }, "name"),
}

-- A published tool is exposed under a prefixed name, so two servers publishing the same tool never collide.
local function qualified(serverId, name)
    local exposed = ("mcp_" .. serverId .. "_" .. name):lower():gsub("[^%w_]", "_")
    return #exposed <= 64 and exposed or nil
end

-- The catalog every agent receives: the native tools, the tools of every ready server and the four that read their resources and prompts.
function tools.schemas(servers)
    local list = {}
    local published = {}

    for _, tool in ipairs(native) do
        list[#list + 1] = tool
    end

    local ready = {}

    for _, client in ipairs(servers or {}) do
        if client.ready then
            ready[#ready + 1] = client
        end
    end

    for _, client in ipairs(ready) do
        for _, tool in ipairs(client.tools) do
            local exposed = qualified(client.descriptor.id, tool.name)
            local declared = exposed ~= nil and published[exposed] == nil and { name = exposed, description = tool.description ~= "" and tool.description or tool.name, summary = tool.description, parameters = tool.inputSchema, serverId = client.descriptor.id, serverTool = tool.name, readOnly = tool.readOnly } or nil

            if declared ~= nil and protocols.validSchema(declared) then
                published[exposed] = declared
                list[#list + 1] = declared
            end
        end
    end

    if #ready > 0 then
        for _, tool in ipairs(serverHelpers) do
            list[#list + 1] = tool
        end
    end

    return list
end

function tools.find(list, name)
    for _, tool in ipairs(list) do
        if tool.name == name then
            return tool
        end
    end

    return nil
end

-- A tool a server published is named the way its server spelled it, with the marks that separate its words read as spaces.
local function readableName(name)
    local words = {}

    for word in (name:match("[^.]+$") or name):gsub("[_-]", " "):gmatch("%S+") do
        words[#words + 1] = word:sub(1, 1):upper() .. word:sub(2)
    end

    return table.concat(words, " ")
end

-- A long argument keeps its beginning and its end, because the end of a path is what names the file.
local function shortened(value)
    local single = spaces.trim((value:gsub("%s+", " ")))
    local length = utf8.len(single) or #single

    if length <= readableArgument then
        return single
    end

    local half = (readableArgument - 1) // 2
    local tail = utf8.offset(single, -half) or (#single - half + 1)
    return single:sub(1, (utf8.offset(single, half + 1) or (half + 1)) - 1) .. "…" .. single:sub(tail)
end

-- A call is shown as the tool it is: a declared one by its title and activity, and a published one by its readable name over the description its server gives.
function tools.presentation(list, name, arguments, translate)
    local declared = tools.find(list, name)

    if declared == nil then
        return { title = readableName(name), activity = "" }
    end

    if declared.titleKey == nil then
        return { title = readableName(name), activity = declared.summary or "" }
    end

    local title = translate(declared.titleKey)
    local value = declared.activityArgument ~= nil and arguments[declared.activityArgument] or nil

    if value == nil then
        return { title = title, activity = "" }
    end

    local written = type(value) == "string" and value or codec.encode(value)
    return { title = title, activity = written ~= "" and translate(declared.activityKey, shortened(written)) or "" }
end

-- A result travels straight into the model context as UTF-8, so bytes that are not text are replaced and an oversized one keeps its beginning and its end, where a command reports what failed.
function tools.bounded(text)
    local readable = utf8.len(text) ~= nil and text or codec.text(text)
    local length = utf8.len(readable)

    if length <= resultHead + resultTail then
        return readable
    end

    local head = readable:sub(1, utf8.offset(readable, resultHead + 1) - 1)
    local tail = readable:sub(utf8.offset(readable, -resultTail))
    return head .. "\n[... " .. (length - resultHead - resultTail) .. " characters omitted ...]\n" .. tail
end

local function segments(path)
    local list = {}

    for part in path:gsub("\\", "/"):gmatch("[^/]+") do
        if part == ".." then
            list[#list] = nil
        elseif part ~= "." then
            list[#list + 1] = part
        end
    end

    return list
end

local function cleaned(path)
    local drive = path:match("^(%a:)[/\\]")
    local joined = table.concat(segments(drive ~= nil and path:sub(3) or path), "/")
    return (drive or "") .. "/" .. joined
end

local function toolFailure(message)
    error({ code = "ai_tool_failed", message = message, detail = "" }, 0)
end

-- Answers whether a folder lists a name, which for a name that does not exist means a link to nothing.
local function listed(folder, name)
    local names = fs.readdir(folder):await()

    for _, entry in ipairs(names or {}) do
        if entry == name then
            return true
        end
    end

    return false
end

-- A tool never leaves the working directory the task declared: the deepest part of the path that exists is resolved, so a link or two dots cannot escape.
-- A link to nothing is refused, since a write through it would create its target wherever it points.
function tools.resolve(root, path, translate)
    if root == nil or root == "" then
        toolFailure(translate("ai.error.tool-workdir-none"))
    end

    local canonicalRoot, missing = workpane.files.canonical(root):await()

    if missing ~= nil then
        toolFailure(translate("ai.error.tool-workdir-unavailable", root))
    end

    local written = path or ""
    local candidate = cleaned(workpane.files.absolute(written) and written or (canonicalRoot .. "/" .. written))
    local ancestor = candidate
    local remainder = {}

    while not fs.exists(ancestor) do
        local parent = ancestor:match("^(.*)/[^/]+$")
        local name = ancestor:match("[^/]+$")

        if parent == nil or parent == "" or parent == ancestor then
            break
        end

        if listed(parent, name) then
            toolFailure(translate("ai.error.tool-path-link", written))
        end

        table.insert(remainder, 1, name)
        ancestor = parent
    end

    local resolved = workpane.files.canonical(ancestor):await()

    if resolved == nil then
        toolFailure(translate("ai.error.tool-path-unresolved", written))
    end

    local contained = #remainder > 0 and (resolved:gsub("/$", "") .. "/" .. table.concat(remainder, "/")) or resolved

    if contained ~= canonicalRoot and contained:sub(1, #canonicalRoot + 1) ~= canonicalRoot .. "/" then
        toolFailure(translate("ai.error.tool-path-outside", written, canonicalRoot))
    end

    return contained
end

-- An unknown name and a server tool may reach anything, so they wait for the turn to be theirs alone, while reads of one turn run together.
function tools.access(list, call, root, translate)
    local declared = tools.find(list, call.name)

    if detached[call.name] or (declared ~= nil and declared.readOnly) then
        return { kind = "none", paths = {} }
    end

    local function located(argument)
        local resolved, path = pcall(tools.resolve, root, call.arguments[argument], translate)
        return resolved and path or nil
    end

    if reading[call.name] or writing[call.name] then
        local path = located("path")
        return path ~= nil and { kind = reading[call.name] and "read" or "write", paths = { path } } or { kind = "none", paths = {} }
    end

    if pairsOfPaths[call.name] then
        local paths = {}

        for _, argument in ipairs({ "source", "destination" }) do
            paths[#paths + 1] = located(argument)
        end

        return #paths > 0 and { kind = "write", paths = paths } or { kind = "none", paths = {} }
    end

    return { kind = "everything", paths = {} }
end

local function overlap(first, second)
    return first == second or first:sub(1, #second + 1) == second .. "/" or second:sub(1, #first + 1) == first .. "/"
end

function tools.conflict(first, second)
    if first.kind == "none" or second.kind == "none" then
        return false
    end

    if first.kind == "everything" or second.kind == "everything" then
        return true
    end

    if first.kind == "read" and second.kind == "read" then
        return false
    end

    for _, left in ipairs(first.paths) do
        for _, right in ipairs(second.paths) do
            if overlap(left, right) then
                return true
            end
        end
    end

    return false
end

-- The model writes the time limit, so one outside the range the catalog declares is refused rather than multiplied into a deadline.
local function commandTimeout(call)
    local declared = call.arguments.timeout_seconds

    if declared == nil then
        return defaultCommandTimeout
    end

    local whole = math.tointeger(declared)

    if whole == nil or whole < 0 or whole > catalog.limit("maximumCommandTimeoutSeconds") then
        return nil
    end

    return whole
end

-- Every tool carries a deadline except a command the model declared unlimited.
function tools.deadline(call)
    local timeout = call.name == "run_command" and commandTimeout(call) or nil

    if timeout == nil then
        return catalog.limit("toolDeadlineMs")
    end

    return timeout == 0 and 0 or timeout * 1000 + catalog.limit("toolDeadlineMs")
end

local function readBounded(path, limit)
    local handle, failure = fs.open(path, "r"):await()

    if failure ~= nil then
        toolFailure(tostring(failure))
    end

    local parts = {}
    local total = 0

    while total < limit do
        local chunk = handle:read(math.min(65536, limit - total)):await()

        if chunk == nil or chunk == "" then
            break
        end

        parts[#parts + 1] = chunk
        total = total + #chunk
    end

    handle:close():await()

    return table.concat(parts)
end

-- A file is read whole or refused, since an edit written over part of a file would lose the rest of it.
local function textOf(path, translate)
    local info = fs.stat(path):await()

    if info == nil or not info.isFile then
        toolFailure(translate("ai.error.tool-path-missing", path))
    end

    local content = readBounded(path, maximumReadBytes + 1)

    if #content > maximumReadBytes then
        toolFailure(translate("ai.error.tool-file-too-large", path))
    end

    if utf8.len(content) == nil then
        toolFailure(translate("ai.error.tool-not-text", path))
    end

    return content
end

local function writeFile(path, content, translate)
    local parent = path:match("^(.*)/[^/]+$")

    if parent ~= nil and parent ~= "" and not fs.exists(parent) then
        fs.mkdir(parent):await()
    end

    local _, failure = fs.writeFile(path, content):await()

    if failure ~= nil then
        toolFailure(translate("ai.error.tool-write-failed", path, tostring(failure)))
    end
end

-- A web answer is read up to its bound, and whatever arrives after the bound is left unheard, while the timeout of the request bounds the whole answer.
local function boundedRequest(options, limit)
    local state = { parts = {}, bytes = 0 }
    local settled, settle = async.deferred()

    workpane.task(function()
        local _, failure = http.client.stream(options, function(chunk)
            if state.bytes < limit then
                state.parts[#state.parts + 1] = chunk:sub(1, limit - state.bytes)
            end

            state.bytes = state.bytes + #chunk

            if state.bytes >= limit then
                settle()
            end
        end):await()
        state.failure = failure
        state.done = true
        settle()
    end)

    settled:await()

    return { body = table.concat(state.parts), truncated = state.bytes > limit, failure = state.failure, done = state.done }
end

local function streamOptions(url, method, headers, body, timeout, state)
    return { url = url, method = method, headers = headers, body = body, timeoutSeconds = timeout, onResponse = function(status)
        state.status = status
    end }
end

-- A service answers a failure with its own reason, and the agent needs that reason to explain what happened.
local function serviceMessage(body, transport)
    local document = codec.read(body or "")

    if type(document) == "table" then
        for _, key in ipairs({ "message", "detail", "error_description", "error" }) do
            local value = document[key]

            if type(value) == "string" and value ~= "" then
                return value
            end

            if type(value) == "table" and type(value.message) == "string" then
                return value.message
            end
        end
    end

    return (body == nil or body == "") and transport or (transport .. ": " .. body:sub(1, 512))
end

local function webRequest(url, method, headers, body, timeout, limit, translate)
    local state = {}
    local options = streamOptions(url, method, headers, body, timeout, state)
    local answer = boundedRequest(options, limit)
    answer.status = state.status
    local failed = answer.failure ~= nil or answer.status == nil or answer.status >= 400

    if failed and not answer.truncated then
        toolFailure(serviceMessage(answer.body, answer.status ~= nil and ("HTTP " .. answer.status) or (answer.failure ~= nil and tostring(answer.failure) or translate("ai.error.tool-request-failed"))))
    end

    return answer
end

-- A page is reduced to its readable words: scripts and styles go, tags become spaces and runs of spaces become one.
local function readableText(html)
    local text = html

    for _, tag in ipairs({ "script", "style" }) do
        local lowered = text:lower()
        local parts = {}
        local position = 1

        while true do
            local open = lowered:find("<" .. tag, position, true)
            local close = open ~= nil and lowered:find("</" .. tag .. ">", open, true) or nil

            if close == nil then
                break
            end

            parts[#parts + 1] = text:sub(position, open - 1)
            position = close + #tag + 3
        end

        parts[#parts + 1] = text:sub(position)
        text = table.concat(parts)
    end

    return spaces.trim((text:gsub("<[^>]*>", " "):gsub("%s+", " ")))
end

-- A name pattern of the published file tools is a glob, where a star is any run of characters and a question mark any one.
local function globPattern(glob)
    local escaped = glob:lower():gsub("[%^%$%(%)%%%.%[%]%+%-]", "%%%0"):gsub("%*", ".*"):gsub("%?", ".")
    return "^" .. escaped .. "$"
end

local implementations = {}

function implementations.read_file(call, context)
    local path = tools.resolve(context.root, call.arguments.path, context.translate)
    local first = math.tointeger(call.arguments.start_line or 0)
    local last = math.tointeger(call.arguments.end_line or 0)

    if first == nil or last == nil or first < 0 or last < 0 or (first > 0 and last > 0 and last < first) then
        toolFailure(context.translate("ai.error.tool-argument-value", call.name, "start_line"))
    end

    local content = textOf(path, context.translate)

    if first <= 0 and last <= 0 then
        return tools.bounded(content)
    end

    local lines = {}

    for line in (content .. "\n"):gmatch("(.-)\n") do
        lines[#lines + 1] = line
    end

    local from = first > 0 and math.min(first, #lines + 1) or 1
    local to = last > 0 and math.min(last, #lines) or #lines
    return tools.bounded(from > to and "" or table.concat(lines, "\n", from, to))
end

function implementations.write_file(call, context)
    local path = tools.resolve(context.root, call.arguments.path, context.translate)
    writeFile(path, call.arguments.content, context.translate)

    return path
end

-- An edit names the passage it replaces, which must appear exactly once unless every occurrence is asked for.
function implementations.edit_file(call, context)
    local path = tools.resolve(context.root, call.arguments.path, context.translate)
    local old, new = call.arguments.old_text, call.arguments.new_text

    if old == "" then
        toolFailure(context.translate("ai.error.tool-argument-value", call.name, "old_text"))
    end

    local content = textOf(path, context.translate)
    local occurrences = 0
    local position = 1

    while true do
        local found = content:find(old, position, true)

        if found == nil then
            break
        end

        occurrences = occurrences + 1
        position = found + #old
    end

    if occurrences == 0 then
        toolFailure(context.translate("ai.error.tool-edit-absent", path))
    end

    if occurrences > 1 and call.arguments.replace_all ~= true then
        toolFailure(context.translate("ai.error.tool-edit-ambiguous", tostring(occurrences)))
    end

    local parts = {}
    position = 1
    local replaced = 0

    while true do
        local found = content:find(old, position, true)

        if found == nil or (replaced > 0 and call.arguments.replace_all ~= true) then
            break
        end

        parts[#parts + 1] = content:sub(position, found - 1) .. new
        position = found + #old
        replaced = replaced + 1
    end

    parts[#parts + 1] = content:sub(position)
    writeFile(path, table.concat(parts), context.translate)

    return path .. " " .. replaced
end

function implementations.read_image(call, context)
    local provider = catalog.provider(context.connection.providerId)

    if provider == nil or not catalog.traits(provider, context.connection.modelId).vision then
        toolFailure(context.translate("ai.error.tool-image-unsupported", context.connection.modelId))
    end

    local path = tools.resolve(context.root, call.arguments.path, context.translate)
    local mediaType = imageTypes[(path:match("%.([^./]+)$") or ""):lower()]

    if mediaType == nil then
        toolFailure(context.translate("ai.error.tool-image-format", path))
    end

    local info = fs.stat(path):await()

    if info == nil or not info.isFile or info.size == 0 or info.size > maximumViewedImageBytes then
        toolFailure(context.translate("ai.error.tool-image-size", path))
    end

    return path, fs.readFile(path):await(), mediaType
end

function implementations.list_directory(call, context)
    local path = tools.resolve(context.root, call.arguments.path, context.translate)
    local entries, failure = workpane.files.list(path):await()

    if failure ~= nil then
        toolFailure(failure.message)
    end

    table.sort(entries, function(first, second)
        return first.name < second.name
    end)

    local lines = {}

    for index = 1, math.min(#entries, maximumListed) do
        lines[index] = entries[index].name .. (entries[index].kind == "directory" and "/" or "")
    end

    return tools.bounded(table.concat(lines, "\n"))
end

function implementations.create_directory(call, context)
    local path = tools.resolve(context.root, call.arguments.path, context.translate)
    local _, failure = fs.mkdir(path):await()

    if failure ~= nil then
        toolFailure(context.translate("ai.error.tool-directory-failed", path, tostring(failure)))
    end

    return "created"
end

local function transfer(call, context, move)
    local source = tools.resolve(context.root, call.arguments.source, context.translate)
    local destination = tools.resolve(context.root, call.arguments.destination, context.translate)

    if not fs.exists(source) then
        toolFailure(context.translate("ai.error.tool-path-missing", call.arguments.source))
    end

    if fs.exists(destination) then
        toolFailure(context.translate("ai.error.tool-destination-exists", call.arguments.destination))
    end

    if move and destination:sub(1, #source + 1) == source .. "/" then
        toolFailure(context.translate("ai.error.tool-move-inside", call.arguments.source))
    end

    local parent = destination:match("^(.*)/[^/]+$")

    if parent ~= nil and not fs.exists(parent) then
        fs.mkdir(parent):await()
    end

    local _, failure = (move and fs.rename(source, destination) or fs.copy(source, destination)):await()

    if failure ~= nil then
        toolFailure(context.translate(move and "ai.error.tool-move-failed" or "ai.error.tool-copy-failed", call.arguments.source, tostring(failure)))
    end

    return move and "moved" or "copied"
end

function implementations.move_path(call, context)
    return transfer(call, context, true)
end

function implementations.copy_file(call, context)
    local source = tools.resolve(context.root, call.arguments.source, context.translate)
    local info = fs.stat(source):await()

    if info ~= nil and not info.isFile then
        toolFailure(context.translate("ai.error.tool-copy-file-only", call.arguments.source))
    end

    return transfer(call, context, false)
end

-- Removing a directory takes everything inside it, so an agent that did not ask for that is told instead of losing the contents.
-- The path is resolved up to the folder holding it and its last name is taken as written, so removing a link removes the link rather than what it points at.
function implementations.remove_path(call, context)
    local written = tostring(call.arguments.path or ""):gsub("[/\\]+$", "")
    local folder, name = written:match("^(.*)[/\\]([^/\\]+)$")

    if name == nil then
        folder, name = ".", written
    end

    if name == "" or name == "." or name == ".." then
        toolFailure(context.translate("ai.error.tool-path-unresolved", call.arguments.path))
    end

    local parent = tools.resolve(context.root, folder == "" and "/" or folder, context.translate)
    local path = parent:gsub("/$", "") .. "/" .. name
    local info = fs.stat(path):await()

    if info == nil and not listed(parent, name) then
        toolFailure(context.translate("ai.error.tool-path-missing", call.arguments.path))
    end

    if info ~= nil and info.isDir and not info.isSymlink and call.arguments.recursive ~= true then
        local entries, unlisted = workpane.files.list(path):await()

        if unlisted ~= nil then
            toolFailure(context.translate("ai.error.tool-remove-failed", call.arguments.path, tostring(unlisted.message)))
        end

        if #entries > 0 then
            toolFailure(context.translate("ai.error.tool-remove-not-empty", call.arguments.path))
        end
    end

    local _, failure = fs.removeRecursive(path):await()

    if failure ~= nil then
        toolFailure(context.translate("ai.error.tool-remove-failed", call.arguments.path, tostring(failure)))
    end

    return "removed"
end

function implementations.describe_path(call, context)
    local path = tools.resolve(context.root, call.arguments.path, context.translate)
    local info = fs.stat(path):await()
    local access = info ~= nil and workpane.files.access(path):await() or nil

    if access == nil then
        toolFailure(context.translate("ai.error.tool-path-missing", call.arguments.path))
    end

    return codec.encode({ type = info.isDir and "directory" or "file", byteSize = info.size, modifiedAtUtc = cron.stamp(info.mtime), readable = access.readable, writable = access.writable })
end

-- The files under a folder that hold a text, found by the search of the product on a worker, named from that folder as the search names them.
local function holding(base, text)
    local found, failure = workpane.files.search(base, { text = text, maximumMatches = maximumSearched, maximumFileBytes = maximumReadBytes, skip = {} }):await()

    if failure ~= nil then
        toolFailure(failure.message)
    end

    local paths = {}
    local seen = {}

    for _, match in ipairs(found.matches) do
        if not seen[match.path] then
            seen[match.path] = true
            paths[#paths + 1] = match.path
        end
    end

    return paths
end

-- A search stays inside the working directory, matches names by their glob, may require a text inside the first mebibyte and stops at its bounded count.
function implementations.search_files(call, context)
    local pattern = spaces.trim(call.arguments.pattern or "")
    local base = tools.resolve(context.root, call.arguments.path, context.translate)

    if pattern == "" then
        toolFailure(context.translate("ai.error.tool-argument-value", call.name, "pattern"))
    end

    local contains = call.arguments.contains or ""
    local candidates

    if contains ~= "" then
        candidates = holding(base, contains)
    else
        local walked, failure = workpane.files.walk(base, { maximum = maximumWalked, skip = {} }):await()

        if failure ~= nil then
            toolFailure(failure.message)
        end

        candidates = walked.paths
    end

    local glob = globPattern(pattern)
    local matches = {}

    for _, relative in ipairs(candidates) do
        if #matches >= maximumListed then
            break
        end

        local name = (relative:match("[^/]+$") or relative):lower()

        if name:match(glob) then
            matches[#matches + 1] = relative
        end
    end

    table.sort(matches)

    return #matches == 0 and context.translate("ai.error.tool-no-match") or tools.bounded(table.concat(matches, "\n"))
end

-- A command the agent runs is bound to the working directory its file tools are bound to, and stopping the task stops it.
function implementations.run_command(call, context)
    local command = spaces.trim(call.arguments.command or "")

    if command == "" or (context.root or "") == "" then
        toolFailure(context.translate("ai.error.tool-sandbox-missing"))
    end

    local timeout = commandTimeout(call)

    if timeout == nil then
        toolFailure(context.translate("ai.error.tool-argument-value", call.name, "timeout_seconds"))
    end

    local program, arguments = commands.shell(command)
    local run = commands.start({ program = program, arguments = arguments, workdir = context.root, timeoutSeconds = timeout })
    running[call.id] = run
    local finished, code, output = pcall(run.await, run)
    running[call.id] = nil

    if run.cancelled then
        toolFailure(context.translate("ai.error.tool-cancelled"))
    end

    if not finished then
        toolFailure(commands.message(code, context.translate))
    end

    local reported = output == "" and context.translate("ai.tool.command-no-output") or output

    if code ~= 0 then
        toolFailure(tools.bounded(context.translate("ai.error.exit-code", tostring(code)) .. "\n" .. reported))
    end

    return tools.bounded(reported)
end

function implementations.fetch_url(call, context)
    local url = call.arguments.url or ""

    if not url:match("^https?://[^/%s]+") then
        toolFailure(context.translate("ai.error.tool-url", url))
    end

    local answer = webRequest(url, "GET", {}, nil, fetchTimeoutSeconds, maximumFetchBytes, context.translate)
    return tools.bounded(readableText(answer.body))
end

-- Every search service answers with its own shape, so each is read by the contract it publishes.
function implementations.web_search(call, context)
    local query = spaces.trim(call.arguments.query or "")
    local settings = context.search
    local address = preferences.searchAddress(settings)

    if query == "" or address == "" then
        toolFailure(context.translate("ai.error.tool-search-unconfigured"))
    end

    local apiKey = connections.secret(settings.apiKey)
    local count = math.max(1, math.min(maximumSearchResults, math.tointeger(call.arguments.count or defaultSearchResults) or defaultSearchResults))
    local answer

    if settings.provider == "tavily" then
        answer = webRequest(address .. "/search", "POST", { ["Content-Type"] = "application/json", Authorization = "Bearer " .. apiKey }, codec.encode({ query = query, max_results = count }), searchTimeoutSeconds, maximumFetchBytes, context.translate)
    elseif settings.provider == "brave" then
        answer = webRequest(address .. "/res/v1/web/search?q=" .. http.urlEncode(query) .. "&count=" .. count, "GET", { ["X-Subscription-Token"] = apiKey, Accept = "application/json" }, nil, searchTimeoutSeconds, maximumFetchBytes, context.translate)
    else
        answer = webRequest(address .. "/search?q=" .. http.urlEncode(query) .. "&format=json", "GET", {}, nil, searchTimeoutSeconds, maximumFetchBytes, context.translate)
    end

    local document = codec.read(answer.body) or {}
    local entries = settings.provider == "brave" and (type(document.web) == "table" and document.web.results or {}) or (document.results or {})
    local lines = {}

    for _, entry in ipairs(type(entries) == "table" and entries or {}) do
        if #lines >= count then
            break
        end

        if type(entry) == "table" then
            local summary = settings.provider == "brave" and entry.description or entry.content
            lines[#lines + 1] = tostring(entry.title or "") .. "\n" .. tostring(entry.url or "") .. "\n" .. readableText(type(summary) == "string" and summary or "")
        end
    end

    return #lines == 0 and context.translate("ai.search.no-result") or tools.bounded(table.concat(lines, "\n\n"))
end

-- An image is generated by the image endpoint of the provider of the default connection and written where the agent asked.
function implementations.generate_image(call, context)
    local prompt = call.arguments.prompt or ""
    local media = context.media

    if prompt:match("^%s*$") or media == nil then
        toolFailure(context.translate("ai.error.tool-image-unconfigured"))
    end

    local path = tools.resolve(context.root, call.arguments.path, context.translate)
    local provider = catalog.provider(media.providerId)
    local url = connections.endpoint(media.providerId, connections.address(media), "image")

    if provider == nil or url == nil then
        toolFailure(context.translate("ai.error.endpoint-unavailable"))
    end

    local endpoint = provider.endpoints.image
    local apiKey = connections.secret(media.apiKey)
    local body = {}

    for key, value in pairs(endpoint.body) do
        body[key] = value
    end

    body.model = endpoint.model ~= "" and endpoint.model or media.modelId
    body[endpoint.textField] = prompt

    if type(call.arguments.size) == "string" and call.arguments.size ~= "" then
        body.size = call.arguments.size
    end

    local headers = { ["Content-Type"] = "application/json" }

    if apiKey ~= "" then
        headers[endpoint.authHeader] = endpoint.authPrefix .. apiKey
    end

    local answer = webRequest(url, "POST", headers, codec.encode(body), mediaTimeoutSeconds, maximumMediaBytes, context.translate)

    if answer.truncated then
        toolFailure(context.translate("ai.error.tool-answer-truncated"))
    end

    local document = codec.read(answer.body) or {}
    local first = type(document.data) == "table" and document.data[1] or nil
    local picture = type(first) == "table" and type(first.b64_json) == "string" and crypto.base64Decode(first.b64_json) or ""

    if picture == "" then
        toolFailure(context.translate("ai.error.tool-image-empty"))
    end

    writeFile(path, picture, context.translate)

    return path
end

-- Speech is asked of the service chosen in the settings, which answers audio bytes rather than a JSON envelope.
function implementations.generate_speech(call, context)
    local text = call.arguments.text or ""
    local settings = context.speech
    local endpoint = preferences.speechEndpoint(settings.provider)
    local voice = spaces.trim(call.arguments.voice or "")
    voice = voice ~= "" and voice or settings.voiceId
    local url = connections.endpoint(settings.provider, "", "speech")

    if text:match("^%s*$") or endpoint == nil or url == nil or voice == "" then
        toolFailure(context.translate("ai.error.tool-speech-unconfigured"))
    end

    -- The voice joins the path before the path is resolved, so a voice the model names can never lead the file out of the working directory.
    local written = (call.arguments.path or ""):gsub("{voice}", function()
        return voice
    end)

    local path = tools.resolve(context.root, written, context.translate)
    local apiKey = connections.secret(settings.apiKey)
    local body = {}

    for key, value in pairs(endpoint.body) do
        body[key] = value
    end

    body[endpoint.textField] = text

    if endpoint.voiceField ~= "" then
        body[endpoint.voiceField] = voice
    end

    if endpoint.model ~= "" then
        body.model = endpoint.model
    end

    local address = url:gsub("{voice}", function()
        return http.urlEncode(voice)
    end)

    local answer = webRequest(address, "POST", { ["Content-Type"] = "application/json", [endpoint.authHeader] = endpoint.authPrefix .. apiKey }, codec.encode(body), mediaTimeoutSeconds, maximumMediaBytes, context.translate)

    if answer.truncated then
        toolFailure(context.translate("ai.error.tool-answer-truncated"))
    end

    if answer.body == "" then
        toolFailure(context.translate("ai.error.tool-speech-empty"))
    end

    writeFile(path, answer.body, context.translate)

    return path
end

function implementations.list_voices(_, context)
    local settings = context.speech
    local endpoint = preferences.speechEndpoint(settings.provider)
    local provider = catalog.provider(settings.provider)

    if endpoint == nil or provider == nil then
        toolFailure(context.translate("ai.error.tool-speech-unconfigured"))
    end

    if #endpoint.voices > 0 then
        return table.concat(endpoint.voices, "\n")
    end

    local apiKey = connections.secret(settings.apiKey)
    local answer = webRequest(provider.baseUrl .. endpoint.voiceCatalogPath, "GET", { [endpoint.authHeader] = endpoint.authPrefix .. apiKey }, nil, searchTimeoutSeconds, maximumFetchBytes, context.translate)
    local document = codec.read(answer.body) or {}
    local lines = {}

    for _, voice in ipairs(type(document.voices) == "table" and document.voices or {}) do
        if type(voice) == "table" then
            lines[#lines + 1] = tostring(voice.voice_id or "") .. " | " .. tostring(voice.name or "") .. " | " .. tostring(voice.category or "")
        end
    end

    if #lines == 0 then
        toolFailure(context.translate("ai.error.tool-voices-empty"))
    end

    return table.concat(lines, "\n")
end

local function skillLine(skill)
    return skill.name .. ": " .. skill.description .. " (" .. skill.path .. ")"
end

function implementations.list_skills(_, context)
    local lines = {}

    for _, skill in ipairs(context.resources().skills) do
        lines[#lines + 1] = skillLine(skill)
    end

    return #lines == 0 and context.translate("ai.skill.none") or tools.bounded(table.concat(lines, "\n"))
end

function implementations.search_skills(call, context)
    local query = spaces.trim(call.arguments.query or "")

    if query == "" then
        toolFailure(context.translate("ai.error.tool-argument-value", call.name, "query"))
    end

    local lines = {}
    local wanted = query:lower()

    for _, skill in ipairs(context.resources().skills) do
        if skill.name:lower():find(wanted, 1, true) or skill.description:lower():find(wanted, 1, true) then
            lines[#lines + 1] = skillLine(skill)
        end
    end

    return #lines == 0 and context.translate("ai.skill.no-match", query) or tools.bounded(table.concat(lines, "\n"))
end

local function namedSkill(call, context)
    local name = spaces.trim(call.arguments.name or "")
    local skill = resources.skill(context.resources(), name)

    if skill == nil then
        toolFailure(context.translate("ai.skill.unknown", name))
    end

    return skill
end

function implementations.read_skill(call, context)
    local skill = namedSkill(call, context)
    local readable, content = pcall(readBounded, skill.path, maximumSkillBytes)

    if not readable then
        toolFailure(context.translate("ai.error.tool-path-missing", skill.path))
    end

    return tools.bounded(content)
end

-- A file inside a folder is named by its path relative to that folder, and a path that names the folder itself or climbs out of it is refused before any link is followed.
local function inside(folder, relative, refusal, context)
    local depth = 0

    for part in relative:gsub("\\", "/"):gmatch("[^/]+") do
        depth = part == ".." and depth - 1 or part == "." and depth or depth + 1

        if depth < 0 then
            toolFailure(context.translate(refusal, relative))
        end
    end

    if depth == 0 or workpane.files.absolute(relative) then
        toolFailure(context.translate(refusal, relative))
    end

    return tools.resolve(folder, relative, context.translate)
end

-- A skill ships its references beside its instructions, and the agent reads them from inside the folder of that skill and nowhere else.
function implementations.read_skill_file(call, context)
    local skill = namedSkill(call, context)
    local relative = spaces.trim(call.arguments.path or "")
    local path = inside(skill.folder, relative, "ai.skill.file-outside", context)
    local readable, content = pcall(readBounded, path, maximumSkillBytes)

    if not readable then
        toolFailure(context.translate("ai.error.tool-path-missing", relative))
    end

    return tools.bounded(content)
end

function implementations.list_agent_plugins(_, context)
    local listed = {}

    for index, plugin in ipairs(context.resources().plugins) do
        local skills = {}

        for _, skill in ipairs(context.resources().skills) do
            if skill.plugin == plugin.name then
                skills[#skills + 1] = skill.name
            end
        end

        listed[index] = { name = plugin.name, version = plugin.version, description = plugin.description, folder = plugin.folder, manifest = plugin.manifest, skills = codec.list(skills), commands = codec.list(plugin.commands), agents = codec.list(plugin.agents), servers = codec.list(plugin.servers), hooks = plugin.hooks, context = plugin.context }
    end

    return #listed == 0 and context.translate("ai.plugin.none") or tools.bounded(codec.pretty(codec.list(listed)))
end

function implementations.read_agent_plugin_file(call, context)
    local name = spaces.trim(call.arguments.plugin or "")
    local plugin = resources.plugin(context.resources(), name)

    if plugin == nil then
        toolFailure(context.translate("ai.plugin.unknown", name))
    end

    local relative = spaces.trim(call.arguments.path or "")
    local path = inside(plugin.folder, relative, "ai.plugin.file-outside", context)
    local readable, content = pcall(readBounded, path, maximumSkillBytes)

    if not readable then
        toolFailure(context.translate("ai.error.tool-path-missing", relative))
    end

    return tools.bounded(content)
end

-- A search reads the text files of the working directory on a worker and answers each matching line with its file and number, leaving out the folders that hold dependencies and builds.
function implementations.search_text(call, context)
    local text = call.arguments.text or ""

    if text:match("%S") == nil then
        toolFailure(context.translate("ai.error.tool-argument-value", call.name, "text"))
    end

    local root = tools.resolve(context.root, call.arguments.path or ".", context.translate)
    local found, failure = workpane.files.search(root, { text = text, maximumMatches = maximumMatches, maximumFileBytes = maximumSearchedFileBytes, skip = searchSkipped }):await()

    if failure ~= nil then
        toolFailure(tostring(failure.message))
    end

    local lines = {}

    for _, match in ipairs(found.matches) do
        lines[#lines + 1] = match.path .. ":" .. tostring(match.line) .. ": " .. match.text
    end

    if not found.complete then
        lines[#lines + 1] = context.translate("ai.search.incomplete")
    end

    if #found.matches == 0 and found.complete then
        return context.translate("ai.search.no-match", root, text)
    end

    return tools.bounded(table.concat(lines, "\n"))
end

local function describedTask(task)
    local described = { id = task.id, title = task.title, description = task.description, prompt = task.prompt, column = task.column, kind = task.executionKind, agentId = task.agentId, workingDirectory = task.workdir, issueUrl = task.issueUrl, command = task.command, createdAt = task.createdAt, updatedAt = task.updatedAt }

    if task.schedule ~= nil then
        described.schedule = { kind = task.schedule.kind, enabled = task.schedule.enabled, onceAt = task.schedule.onceAt, intervalSeconds = task.schedule.intervalSeconds, cron = task.schedule.cron, timeZone = task.schedule.timeZone, nextRunAt = task.schedule.nextRunAt }
    end

    return described
end

function implementations.describe_task(_, context)
    return codec.pretty(describedTask(context.task))
end

function implementations.list_tasks(_, context)
    local listed = {}

    for index, task in ipairs(context.tasks()) do
        listed[index] = { id = task.id, title = task.title, column = task.column, kind = task.executionKind, agentId = task.agentId, current = task.id == context.task.id }
    end

    return tools.bounded(codec.pretty(codec.list(listed)))
end

-- Another task of the board is read with its latest runs, so an agent learns what was done before it without reading every conversation.
function implementations.read_task(call, context)
    local id = spaces.trim(call.arguments.id or "")

    for _, task in ipairs(context.tasks()) do
        if task.id == id then
            local described = describedTask(task)
            local runs = {}

            for index, row in ipairs(workpane.await(store.executions(task.id, recentRuns))) do
                runs[index] = { status = row.status, startedAt = row.started_at_utc, finishedAt = row.finished_at_utc, stopReason = row.stop_reason, error = row.error_message, answer = row.content }
            end

            described.runs = codec.list(runs)

            return tools.bounded(codec.pretty(described))
        end
    end

    toolFailure(context.translate("ai.workpane.task-unknown", id))
end

function implementations.list_workpane_plugins(_, _)
    local listed = {}

    for index, plugin in ipairs(workpane.app.plugins()) do
        listed[index] = { id = plugin.id, title = plugin.titleKey ~= nil and workpane.i18n.translate(plugin.titleKey) or plugin.id, description = plugin.descriptionKey ~= nil and workpane.i18n.translate(plugin.descriptionKey) or "", state = plugin.state }
    end

    return tools.bounded(codec.pretty(codec.list(listed)))
end

function implementations.list_workpane_capabilities(_, _)
    local listed = {}

    for _, capability in ipairs(workpane.capabilities.list()) do
        if capability.agents then
            listed[#listed + 1] = { name = capability.name, provider = capability.provider, summary = workpane.i18n.translate(capability.summaryKey), payload = capability.payload }
        end
    end

    return tools.bounded(codec.pretty(codec.list(listed)))
end

-- An agent asks only for a capability whose provider opens it to agents, so no run erases what the reader keeps, reaches outside its folder or starts another run.
function implementations.request_workpane_capability(call, context)
    local name = spaces.trim(call.arguments.name or "")
    local opened = false

    for _, capability in ipairs(workpane.capabilities.list()) do
        opened = opened or (capability.name == name and capability.agents)
    end

    if not opened then
        toolFailure(context.translate("ai.workpane.capability-closed", name))
    end

    local answer, failure = workpane.capabilities.request(name, codec.plain(call.arguments.payload)):await()

    if failure ~= nil then
        toolFailure(context.translate("ai.workpane.capability-failed", name, tostring(failure.code), tostring(failure.message)))
    end

    return tools.bounded(codec.pretty(answer))
end

-- The servers are looked up when a call runs, so a call reaches the client of a server that restarted since the run began.
local function readyServer(context, id)
    for _, client in ipairs(context.servers()) do
        if client.ready and client.descriptor.id == id then
            return client
        end
    end

    return nil
end

local function serverCatalog(context, prompts)
    local sections = {}
    local ready = 0

    for _, client in ipairs(context.servers()) do
        if client.ready then
            ready = ready + 1
            local answered, result = pcall(prompts and client.listPrompts or client.listResources, client)
            local entries = answered and type(result) == "table" and result[prompts and "prompts" or "resources"] or {}

            for _, entry in ipairs(type(entries) == "table" and entries or {}) do
                if type(entry) == "table" then
                    sections[#sections + 1] = client.descriptor.id .. " | " .. tostring(prompts and entry.name or entry.uri) .. " | " .. tostring(entry.description or "")
                end
            end
        end
    end

    if ready == 0 then
        toolFailure(context.translate("ai.error.mcp-none"))
    end

    return tools.bounded(table.concat(sections, "\n"))
end

function implementations.list_mcp_resources(_, context)
    return serverCatalog(context, false)
end

function implementations.list_mcp_prompts(_, context)
    return serverCatalog(context, true)
end

function implementations.read_mcp_resource(call, context)
    local client = readyServer(context, call.arguments.server or "")

    if client == nil or (call.arguments.uri or "") == "" then
        toolFailure(context.translate("ai.error.mcp-unavailable", call.name))
    end

    local result = client:readResource(call.arguments.uri)
    local sections = {}

    for _, content in ipairs(type(result.contents) == "table" and result.contents or {}) do
        sections[#sections + 1] = type(content) == "table" and type(content.text) == "string" and content.text or ""
    end

    return tools.bounded(table.concat(sections, "\n"))
end

function implementations.read_mcp_prompt(call, context)
    local client = readyServer(context, call.arguments.server or "")

    if client == nil or (call.arguments.name or "") == "" then
        toolFailure(context.translate("ai.error.mcp-unavailable", call.name))
    end

    local result = client:getPrompt(call.arguments.name, type(call.arguments.arguments) == "table" and call.arguments.arguments or {})
    local sections = {}

    for _, message in ipairs(type(result.messages) == "table" and result.messages or {}) do
        local content = type(message) == "table" and message.content or nil
        sections[#sections + 1] = type(content) == "table" and type(content.text) == "string" and content.text or ""
    end

    return tools.bounded(table.concat(sections, "\n"))
end

-- A server answers with content blocks, and its textual blocks become the result the agent reads, while stopping the task withdraws the call at the server.
local function serverTool(call, declared, context)
    local client = readyServer(context, declared.serverId)

    if client == nil then
        error({ code = "ai_tool_failed", message = "unavailable", detail = "" }, 0)
    end

    local watched, result = pcall(client.callTool, client, declared.serverTool, call.arguments, tools.deadline(call), function(handle)
        running[call.id] = handle
    end)

    running[call.id] = nil

    if not watched then
        error(result, 0)
    end

    local sections = {}

    for _, block in ipairs(type(result.content) == "table" and result.content or {}) do
        if type(block) == "table" and block.type == "text" and type(block.text) == "string" then
            sections[#sections + 1] = block.text
        elseif type(block) == "table" and block.type == "resource" and type(block.resource) == "table" and type(block.resource.text) == "string" then
            sections[#sections + 1] = block.resource.text
        end
    end

    return tools.bounded(table.concat(sections, "\n")), result.isError == true
end

-- A call is judged against the schema the model received, so what it got wrong is named instead of leaving it to guess and repeat.
function tools.invoke(list, call, context)
    local declared = tools.find(list, call.name)

    if declared == nil then
        return { callId = call.id, text = context.translate("ai.error.tool-unknown", call.name), failed = true }
    end

    local wrong = protocols.argumentError(declared, call.arguments)

    if wrong ~= nil then
        local key = call.arguments[wrong.argument] ~= nil and "ai.error.tool-argument-type" or "ai.error.tool-argument-missing"
        return { callId = call.id, text = context.translate(key, call.name, wrong.argument, wrong.expected), failed = true }
    end

    local answered, text, second, third

    if declared.serverId ~= nil then
        answered, text, second = pcall(serverTool, call, declared, context)

        if not answered and type(text) == "table" and text.message == "unavailable" then
            return { callId = call.id, text = context.translate("ai.error.mcp-unavailable", call.name), failed = true }
        end
    else
        answered, text, second, third = pcall(implementations[call.name], call, context)
    end

    if not answered then
        return { callId = call.id, text = type(text) == "table" and tostring(text.message) or tostring(text), failed = true }
    end

    -- A result that says nothing is told as such, because a conversation never carries a message without content.
    local blank = type(text) ~= "string" or text:match("%S") == nil
    local answer = blank and (declared.serverId ~= nil or second == nil) and context.translate("ai.tool.result-empty") or text

    if declared.serverId ~= nil then
        return { callId = call.id, text = answer, failed = second }
    end

    return { callId = call.id, text = answer, failed = false, imageData = second, imageMediaType = third }
end

-- Stopping a task stops the command still running for one of its calls.
function tools.cancel(callId)
    local run = running[callId]

    if run ~= nil then
        running[callId] = nil
        run:cancel()
    end
end

return tools
