-- Runs programs for plugins and hands each plugin what its programs wrote and the code each of them ended with.
local bridge = require("workpane.bridge")
local task = require("workpane.task")

local process = {}

local handlers = {}

-- A program starts at once, so its handlers are in place before anything it writes can arrive.
function process.start(owner, options)
    if type(options) ~= "table" or (options.onOutput ~= nil and type(options.onOutput) ~= "function") or (options.onExit ~= nil and type(options.onExit) ~= "function") or (options.input ~= nil and type(options.input) ~= "string") then
        bridge.raise("process_options_invalid", "A program is started with a table whose handlers are functions and whose input is a text", "")
    end

    local reply = bridge.call("workpane_process_start", { plugin = owner, program = options.program, arguments = options.arguments or {}, directory = options.directory, variables = options.variables or {}, cleared = options.cleared or {}, input = options.input })
    handlers[reply.process] = { owner = owner, onOutput = options.onOutput, onExit = options.onExit }

    return reply.process
end

function process.write(owner, identity, text)
    bridge.call("workpane_process_write", { plugin = owner, process = identity, text = text })
end

function process.stop(owner, identity)
    bridge.call("workpane_process_stop", { plugin = owner, process = identity })
end

function process.find(owner, name, directories)
    local found = bridge.request("workpane_process_find", { plugin = owner, name = name, directories = directories or {} })

    return bridge.future(function()
        local reply, failure = found:await()

        if failure ~= nil then
            error(failure, 0)
        end

        return reply.found and reply.path or nil
    end)
end

function process.forget(owner)
    for identity, handler in pairs(handlers) do
        if handler.owner == owner then
            handlers[identity] = nil
        end
    end

    bridge.call("workpane_process_forget", { plugin = owner })
end

-- Output is handed over in the order it arrived, so a handler that reads a protocol keeps its buffer in order as long as it does not wait.
bridge.on("workpane.process.output", function(payload)
    local handler = handlers[payload.process]

    if handler ~= nil and handler.owner == payload.plugin and handler.onOutput ~= nil then
        task.run(handler.owner, "process", handler.onOutput, payload.chunks)
    end
end)

bridge.on("workpane.process.exit", function(payload)
    local handler = handlers[payload.process]
    handlers[payload.process] = nil

    if handler ~= nil and handler.owner == payload.plugin and handler.onExit ~= nil then
        task.run(handler.owner, "process", handler.onExit, payload.code, payload.crashed)
    end
end)

return process
