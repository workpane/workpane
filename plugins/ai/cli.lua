-- Holds a turn with a command line agent: the conversation rendered as one prompt, the program invoked in the working directory and its printed answer.
local catalog = include("catalog")
local commands = include("commands")
local diagnostics = include("diagnostics")
local filesystem = include("filesystem")
local messages = include("messages")

local cli = {}

local Client = {}
Client.__index = Client

local charactersPerToken = 4
local headings = { system = "Instructions", assistant = "Assistant" }

-- A program started from the desktop may miss the search path of a shell, so the folders where package managers install agents are searched as well.
local function directories(backend)
    local home = backend.remote and backend.account.home or workpane.system.home()
    local list = backend.platform == "windows" and { home .. "/AppData/Local/Programs", home .. "/AppData/Roaming/npm", home .. "/scoop/shims" } or { "/opt/homebrew/bin", "/usr/local/bin", "/usr/bin", "/opt/local/bin", "/snap/bin" }

    for _, relative in ipairs({ ".local/bin", ".bun/bin", ".deno/bin", ".cargo/bin", ".npm-global/bin" }) do
        list[#list + 1] = home .. "/" .. relative
    end

    return list
end

-- The conversation arrives fitted to a model that reads text alone, so every message is its text under the heading of its role.
function cli.render(list)
    local rendered = {}

    for _, message in ipairs(list) do
        local text = messages.text(message)

        if text ~= "" then
            rendered[#rendered + 1] = "## " .. (headings[message.role] or "User") .. "\n\n" .. text
        end
    end

    return table.concat(rendered, "\n\n")
end

-- The prompt, the working directory and the model replace whole arguments, so nothing is spliced into a string a shell would read.
function cli.arguments(descriptor, promptText, workdir, model)
    local arguments = {}

    for index, argument in ipairs(descriptor.arguments) do
        arguments[index] = argument == "{prompt}" and promptText or argument == "{workdir}" and workdir or argument == "{model}" and model or argument
    end

    return arguments
end

function cli.new(handlers)
    return setmetatable({ handlers = handlers }, Client)
end

function Client:cancel()
    self.cancelled = true

    if self.run ~= nil then
        self.run:cancel()
    end
end

local function refuse(code, message, detail)
    error({ code = code, message = message, detail = detail or "" }, 0)
end

-- A command line agent runs its own tools, so its turn is the whole run of the program, counted at four characters a token since it reports no usage.
function Client:send(request, translate)
    local provider = catalog.provider(request.connection.providerId)

    if provider == nil or provider.protocol ~= "command-line" then
        refuse("ai_cli_provider_invalid", translate("ai.error.cli-provider-invalid"), request.connection.providerId)
    end

    if (request.workdir or "") == "" then
        refuse("ai_cli_workdir_required", translate("ai.error.cli-workdir-required"), provider.id)
    end

    local backend = request.backend or filesystem.native()
    local program = workpane.await(backend.process.find(provider.commandLine.program, directories(backend)))

    -- A request cancelled while its program was being found never starts it.
    if self.cancelled then
        refuse("ai_cancelled", "The request was cancelled", "")
    end

    if program == nil then
        refuse("ai_cli_program_missing", translate("ai.error.cli-program-missing"), provider.commandLine.program)
    end

    -- A program that reads its prompt from its input receives it there, which no limit of a command line or of a batch file on Windows bounds.
    local promptText = cli.render(request.messages)
    local arguments = cli.arguments(provider.commandLine, promptText, request.workdir, request.connection.modelId)
    local cleared = {}

    for index, name in ipairs(provider.commandLine.clearedVariables) do
        cleared[index] = name
    end

    local diagnostic = diagnostics.new()
    self.handlers.requestSent(program, #promptText, #request.messages)
    self.run = commands.start({ backend = backend, program = program, arguments = arguments, workdir = request.workdir, cleared = cleared, input = provider.commandLine.promptInput and promptText or "", timeoutSeconds = 0, outputStream = "output", onOutput = self.handlers.content, onError = provider.id == "codex-cli" and function(text)
        diagnostic:receive(text)
    end or nil, cancelled = function()
        return self.cancelled
    end })

    local finished, code, output = pcall(self.run.await, self.run)

    if self.cancelled then
        refuse("ai_cancelled", "The request was cancelled", "")
    end

    if not finished then
        code.message = commands.message(code, translate)
        error(code, 0)
    end

    local printed = output:match("^%s*(.-)%s*$")

    if code ~= 0 then
        local failure = diagnostic:finish() or { code = "ai_cli_failed" }
        failure.message = failure.code == "ai_input_too_large" and translate("ai.error.input-too-large") or translate("ai.error.exit-code", tostring(code))
        failure.detail = ""
        failure.metadata = failure.metadata or {}
        failure.metadata.exitCode = code
        error(failure, 0)
    end

    return { content = printed, calls = {}, usage = { input = #promptText // charactersPerToken, output = #printed // charactersPerToken }, finishReason = "stop" }
end

return cli
