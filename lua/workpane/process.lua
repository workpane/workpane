-- Runs programs for plugins and hands each plugin what its programs wrote and the code each of them ended with.
local bridge = require("workpane.bridge")
local crypto = require("crypto")
local task = require("workpane.task")

local process = {}

local handlers = {}

local function validText(value)
    return type(value) == "string" and value:find("%z") == nil and utf8.len(value) ~= nil
end

local function validTexts(values)
    if type(values) ~= "table" then
        return false
    end

    for index, value in pairs(values) do
        if math.type(index) ~= "integer" or index < 1 or index > #values or not validText(value) then
            return false
        end
    end

    return true
end

-- A program starts at once, so its handlers are in place before anything it writes can arrive.
function process.start(owner, options)
    if type(options) ~= "table" or (options.onOutput ~= nil and type(options.onOutput) ~= "function") or (options.onExit ~= nil and type(options.onExit) ~= "function") or (options.input ~= nil and type(options.input) ~= "string") or (options.binaryOutput ~= nil and type(options.binaryOutput) ~= "boolean") or (options.pullOutput ~= nil and type(options.pullOutput) ~= "boolean") or (options.pullOutput == true and options.onOutput ~= nil) then
        bridge.raise("process_options_invalid", "A program is started with a table whose handlers are functions, whose input is a string and whose binary output choice is a boolean", "")
    end

    if not validText(options.program) or not validText(options.directory) or options.arguments ~= nil and not validTexts(options.arguments) or options.cleared ~= nil and not validTexts(options.cleared) or options.variables ~= nil and type(options.variables) ~= "table" then
        bridge.raise("process_launch_invalid", "The program launch contains invalid text", "")
    end

    for name, value in pairs(options.variables or {}) do
        if not validText(name) or not validText(value) then
            bridge.raise("process_launch_invalid", "The program environment contains invalid text", "")
        end
    end

    if options.input ~= nil and #options.input > 64 * 1024 * 1024 then
        bridge.raise("process_input_too_large", "The program input exceeds its byte bound", "")
    end

    if options.terminal ~= nil and (type(options.terminal) ~= "table" or math.type(options.terminal.columns) ~= "integer" or options.terminal.columns < 1 or options.terminal.columns > 10000 or math.type(options.terminal.rows) ~= "integer" or options.terminal.rows < 1 or options.terminal.rows > 10000 or options.input ~= nil) then
        bridge.raise("terminal_launch_invalid", "A terminal program declares bounded dimensions and keeps its input open", "")
    end

    local reply = bridge.call("workpane_process_start", { plugin = owner, program = options.program, arguments = options.arguments or {}, directory = options.directory, variables = options.variables or {}, cleared = options.cleared or {}, inputBase64 = options.input ~= nil and crypto.base64Encode(options.input) or nil, binaryOutput = options.binaryOutput == true, pullOutput = options.pullOutput == true, terminal = options.terminal })
    handlers[reply.process] = { owner = owner, onOutput = options.onOutput, onExit = options.onExit, binaryOutput = options.binaryOutput == true or options.pullOutput == true }

    return reply.process
end

function process.write(owner, identity, bytes)
    if type(bytes) ~= "string" then
        bridge.raise("process_input_invalid", "A program receives binary input as a string", "")
    end

    if #bytes > 64 * 1024 * 1024 then
        bridge.raise("process_input_too_large", "The program input exceeds its byte bound", "")
    end

    bridge.call("workpane_process_write", { plugin = owner, process = identity, content = crypto.base64Encode(bytes) })
end

function process.endInput(owner, identity)
    bridge.call("workpane_process_end_input", { plugin = owner, process = identity })
end

function process.stop(owner, identity)
    bridge.call("workpane_process_stop", { plugin = owner, process = identity })
end

function process.resize(owner, identity, columns, rows)
    if math.type(columns) ~= "integer" or columns < 1 or columns > 10000 or math.type(rows) ~= "integer" or rows < 1 or rows > 10000 then
        bridge.raise("terminal_size_invalid", "The terminal dimensions are outside their bounds", "")
    end

    bridge.call("workpane_process_resize", { plugin = owner, process = identity, columns = columns, rows = rows })
end

function process.read(owner, identity, maximum)
    return task.future(owner, function()
        maximum = maximum or 65536

        if math.type(maximum) ~= "integer" or maximum < 1 or maximum > 65536 then
            bridge.raise("process_read_invalid", "A program output read has a bounded byte count", "")
        end

        local reply = task.await(bridge.request("workpane_process_read", { plugin = owner, process = identity, maximum = maximum }))
        local chunks = {}

        for index, chunk in ipairs(reply.chunks) do
            chunks[index] = { stream = chunk.stream, text = crypto.base64Decode(chunk.content) }
        end

        reply.chunks = chunks

        return reply
    end)
end

function process.find(owner, name, directories)
    return task.future(owner, function()
        if not validText(name) or directories ~= nil and not validTexts(directories) then
            bridge.raise("process_find_invalid", "The executable name or search directories contain invalid text", "")
        end

        local found = bridge.request("workpane_process_find", { plugin = owner, name = name, directories = directories or {} })
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
        local chunks = payload.chunks

        if handler.binaryOutput then
            chunks = {}

            for index, chunk in ipairs(payload.chunks) do
                chunks[index] = { stream = chunk.stream, text = crypto.base64Decode(chunk.content) }
            end
        end

        task.run(handler.owner, "process", handler.onOutput, chunks)
    end
end)

bridge.on("workpane.process.exit", function(payload)
    local handler = handlers[payload.process]
    handlers[payload.process] = nil

    if handler ~= nil and handler.owner == payload.plugin and handler.onExit ~= nil then
        task.run(handler.owner, "process", handler.onExit, payload.code, payload.failed)
    end
end)

return process
