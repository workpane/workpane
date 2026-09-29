-- Runs a command through the shell of the platform or a program directly, in a working directory, with a time limit, and answers its plain output.
local async = require("async")
local fs = require("fs")

local commands = {}

local Run = {}
Run.__index = Run

local maximumOutputBytes = 1 << 22

local failures = {
    ai_command_timeout = "ai.error.command-timeout",
    ai_command_output_too_large = "ai.error.command-output-too-large",
    ai_command_workdir_invalid = "ai.error.command-workdir-invalid",
    ai_command_failed = "ai.error.command-start-failed",
    ai_command_crashed = "ai.error.command-crashed",
}

-- A failure the reader can act on is named in their language, and any other keeps its diagnostic.
function commands.message(failure, translate)
    local key = failures[failure.code]
    return key ~= nil and translate(key) or failure.message
end

-- Colors, cursor movements and window titles are dropped, a lone carriage return reads as a new line, and a sequence split between two reads waits for its end.
function commands.plain(pending, chunk)
    local source = pending .. chunk
    local plain = {}
    local index = 1

    while index <= #source do
        local character = source:sub(index, index)

        if character == "\27" then
            local introducer = source:sub(index + 1, index + 1)

            if introducer == "" then
                return table.concat(plain), source:sub(index)
            end

            if introducer == "[" then
                local finish = source:find("[\64-\126]", index + 2)

                if finish == nil then
                    return table.concat(plain), source:sub(index)
                end

                index = finish + 1
            elseif introducer == "]" then
                local bell = source:find("\7", index + 2, true)
                local terminator = source:find("\27\\", index + 2, true)

                if bell == nil and terminator == nil then
                    return table.concat(plain), source:sub(index)
                end

                index = (terminator == nil or (bell ~= nil and bell < terminator)) and bell + 1 or terminator + 2
            else
                index = index + 2
            end
        elseif character == "\r" then
            local following = source:sub(index + 1, index + 1)

            if following == "" then
                return table.concat(plain), source:sub(index)
            end

            if following ~= "\n" then
                plain[#plain + 1] = "\n"
            end

            index = index + 1
        elseif character == "\n" or character == "\t" then
            plain[#plain + 1] = character
            index = index + 1
        elseif character:byte() < 32 then
            index = index + 1
        else
            local stop = source:find("[\0-\31]", index) or #source + 1
            plain[#plain + 1] = source:sub(index, stop - 1)
            index = stop
        end
    end

    return table.concat(plain), ""
end

-- The shell of the platform runs a command written as one line, and on Windows that shell is the command prompt.
function commands.shell(command)
    if workpane.app.platform == "windows" then
        local prompt = workpane.await(workpane.process.find("cmd", {}))
        return prompt, { "/d", "/s", "/c", command }
    end

    return "/bin/sh", { "-lc", command }
end

-- Starts at once and answers a run the caller awaits or cancels, with its input closed so a command never waits to be piped to.
function commands.start(options)
    local run = setmetatable({ output = {}, bytes = 0, pending = "", done = false, waiting = {}, onOutput = options.onOutput }, Run)
    local info = fs.stat(options.workdir or ""):await()

    if not workpane.files.absolute(options.workdir or "") or info == nil or not info.isDir then
        run.failure = { code = "ai_command_workdir_invalid", message = "The command working directory is unavailable", detail = options.workdir or "" }
        run.done = true
        return run
    end

    -- A run stopped while its folder was being checked never starts its program.
    if options.cancelled ~= nil and options.cancelled() then
        run.failure = { code = "ai_command_cancelled", message = "The command was cancelled", detail = "" }
        run.done = true
        return run
    end

    local started, identity = pcall(workpane.process.start, { program = options.program, arguments = options.arguments, directory = options.workdir, cleared = options.cleared or {}, input = options.input or "", onOutput = function(chunks)
        run:receive(chunks)
    end, onExit = function(code, crashed)
        run.code = code
        run.crashed = crashed
        run:finish()
    end })

    if not started then
        run.failure = { code = "ai_command_failed", message = type(identity) == "table" and identity.message or tostring(identity), detail = options.program }
        run.done = true
        return run
    end

    run.process = identity

    -- The time limit is raced with the end of the run, so a command that ends early leaves nothing waiting for the limit.
    if (options.timeoutSeconds or 0) > 0 then
        workpane.task(function()
            local ended, resolve = async.deferred()
            run.waiting[#run.waiting + 1] = resolve
            async.timeout(ended, options.timeoutSeconds * 1000):await()

            if not run.done then
                run:fail({ code = "ai_command_timeout", message = "The command exceeded its time limit", detail = "" })
            end
        end)
    end

    return run
end

function Run:receive(chunks)
    for _, chunk in ipairs(chunks) do
        local text
        text, self.pending = commands.plain(self.pending, chunk.text)

        if text ~= "" and self.failure == nil then
            self.bytes = self.bytes + #text

            if self.bytes > maximumOutputBytes then
                self:fail({ code = "ai_command_output_too_large", message = "The command output exceeded the permitted size", detail = "" })
                return
            end

            self.output[#self.output + 1] = text

            if self.onOutput ~= nil then
                self.onOutput(text)
            end
        end
    end
end

-- Every caller waiting for the run continues once it ends.
function Run:finish()
    local waiting = self.waiting
    self.done = true
    self.waiting = {}

    for _, resolve in ipairs(waiting) do
        resolve()
    end
end

function Run:fail(failure)
    self.failure = self.failure or failure
    self:finish()

    if self.process ~= nil then
        pcall(workpane.process.stop, self.process)
    end
end

-- A stopped run ends its program and everything that program started.
function Run:cancel()
    self.cancelled = true
    self:fail({ code = "ai_command_cancelled", message = "The command was cancelled", detail = "" })
end

-- Answers what the program printed so far, which a run that failed still has.
function Run:printed()
    return table.concat(self.output)
end

-- Waits for the program and answers its exit code and output, or raises the failure that ended it, a crash included.
function Run:await()
    if not self.done then
        local ended, resolve = async.deferred()
        self.waiting[#self.waiting + 1] = resolve
        ended:await()
    end

    if self.failure == nil and self.crashed then
        self.failure = { code = "ai_command_crashed", message = "The command terminated abnormally", detail = tostring(self.code) }
    end

    if self.failure ~= nil then
        error(self.failure, 0)
    end

    return self.code, self:printed()
end

return commands
