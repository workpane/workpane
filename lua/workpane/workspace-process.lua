local async = require("async")
local crypto = require("crypto")
local plain = require("workpane.plain")

local workspaceProcess = {}

local maximumInputBytes = 16 * 1024 * 1024
local maximumInputMessages = 4096
local maximumChunkBytes = 65536

local function failure(code)
    return { code = code, message = "The remote workspace program could not be completed", detail = "" }
end

local function text(value, maximum, empty)
    return type(value) == "string" and #value <= maximum and (empty or #value > 0) and value:find("%z") == nil and utf8.len(value) ~= nil
end

local function texts(value, maximumBytes, empty)
    if not plain.table(value) or #value > 256 then
        error(failure("workspace_process_invalid"), 0)
    end

    local copied = {}
    local count = 0

    for key, entry in pairs(value) do
        if math.type(key) ~= "integer" or key < 1 or key > #value or not text(entry, maximumBytes, empty) then
            error(failure("workspace_process_invalid"), 0)
        end

        count = count + 1
        copied[key] = entry
    end

    if count ~= #value then
        error(failure("workspace_process_invalid"), 0)
    end

    return copied
end

local function launch(options)
    if not plain.table(options) or not text(options.program, 4096) or not text(options.directory, 4096) or type(options.onOutput) ~= "function" or type(options.onExit) ~= "function" then
        error(failure("workspace_process_invalid"), 0)
    end

    local fields = { program = true, directory = true, arguments = true, variables = true, cleared = true, input = true, onOutput = true, onExit = true }

    for key in pairs(options) do
        if fields[key] == nil then
            error(failure("workspace_process_invalid"), 0)
        end
    end

    local arguments = texts(options.arguments == nil and {} or options.arguments, 65536, true)
    local cleared = texts(options.cleared == nil and {} or options.cleared, 65536)

    for _, name in ipairs(cleared) do
        if name:find("=", 1, true) ~= nil then
            error(failure("workspace_process_invalid"), 0)
        end
    end

    local variables = {}
    local declared = options.variables == nil and {} or options.variables

    if not plain.table(declared) then
        error(failure("workspace_process_invalid"), 0)
    end

    local count = 0

    for name, value in pairs(declared) do
        count = count + 1

        if count > 256 or not text(name, 256) or name:find("=", 1, true) ~= nil or not text(value, 65536, true) then
            error(failure("workspace_process_invalid"), 0)
        end

        variables[name] = value
    end

    if options.input ~= nil and (type(options.input) ~= "string" or #options.input > 4 * 1024 * 1024) then
        error(failure("workspace_process_invalid"), 0)
    end

    return { program = options.program, directory = options.directory, arguments = arguments, variables = variables, cleared = cleared, input = options.input ~= nil and crypto.base64Encode(options.input) or nil }
end

local function output(value)
    if type(value) ~= "table" or type(value.ended) ~= "boolean" or type(value.failed) ~= "boolean" or math.type(value.code) ~= "integer" or value.code < -2147483648 or value.code > 2147483647 or type(value.chunks) ~= "table" or #value.chunks > 1024 then
        error(failure("workspace_response_invalid"), 0)
    end

    for key in pairs(value) do
        if key ~= "ended" and key ~= "failed" and key ~= "code" and key ~= "chunks" then
            error(failure("workspace_response_invalid"), 0)
        end
    end

    local chunks = {}
    local bytes = 0
    local count = 0

    for key, chunk in pairs(value.chunks) do
        if math.type(key) ~= "integer" or key < 1 or key > #value.chunks or type(chunk) ~= "table" or chunk.stream ~= "stdout" and chunk.stream ~= "stderr" or type(chunk.content) ~= "string" or #chunk.content > 87384 then
            error(failure("workspace_response_invalid"), 0)
        end

        for field in pairs(chunk) do
            if field ~= "stream" and field ~= "content" then
                error(failure("workspace_response_invalid"), 0)
            end
        end

        local decoded, text = pcall(crypto.base64Decode, chunk.content)

        if not decoded or type(text) ~= "string" or #text > maximumChunkBytes - bytes or crypto.base64Encode(text) ~= chunk.content then
            error(failure("workspace_response_invalid"), 0)
        end

        count = count + 1
        bytes = bytes + #text
        chunks[key] = { stream = chunk.stream == "stdout" and "output" or "error", text = text }
    end

    if count ~= #value.chunks then
        error(failure("workspace_response_invalid"), 0)
    end

    return chunks
end

function workspaceProcess.new(backend, workpane)
    local adapter = {}
    local operations = {}

    local function finish(operation, code, unsuccessful, problem)
        if operation.ended then
            return
        end

        operation.ended = true
        operation.input = {}
        operation.bytes = 0
        operations[operation] = nil

        if operation.id ~= nil then
            workpane.task(operation.options.onExit, code, unsuccessful, problem)
        end
    end

    local function owned(operation)
        if operations[operation] ~= true or operation.ended then
            error(failure("remote_process_missing"), 0)
        end
    end

    local function terminate(operation, problem)
        if not operation.ended and backend.available and operation.id ~= nil then
            pcall(backend.request, "process.stop", { process = operation.id })
        end

        finish(operation, -1, true, problem or failure("workspace_closed"))
    end

    adapter.find = function(program, directories)
        if not text(program, 4096) then
            error(failure("workspace_process_invalid"), 0)
        end

        local searched = texts(directories == nil and {} or directories, 4096)

        for _, directory in ipairs(searched) do
            if not backend.paths.absolute(directory) then
                error(failure("workspace_process_invalid"), 0)
            end
        end

        return workpane.future(function()
            local result = backend.request("process.find", { program = program, directories = searched })

            if type(result) ~= "table" or type(result.found) ~= "boolean" or not text(result.path, 4096, true) then
                error(failure("workspace_response_invalid"), 0)
            end

            for key in pairs(result) do
                if key ~= "found" and key ~= "path" then
                    error(failure("workspace_response_invalid"), 0)
                end
            end

            if result.found and not backend.paths.absolute(result.path) or not result.found and result.path ~= "" then
                error(failure("workspace_response_invalid"), 0)
            end

            return result.found and result.path or nil
        end)
    end

    adapter.start = function(options)
        local parameters = launch(options)

        if not backend.paths.absolute(parameters.directory) then
            error(failure("workspace_process_invalid"), 0)
        end

        local handlers = { onOutput = options.onOutput, onExit = options.onExit }

        return workpane.future(function()
            local operation = { owner = adapter, options = handlers, input = {}, bytes = 0, writing = false, stopping = false, ended = false }
            operations[operation] = true
            local started, result = pcall(backend.request, "process.start", parameters)

            if not started then
                operations[operation] = nil
                error(result, 0)
            end

            if type(result) ~= "table" or math.type(result.process) ~= "integer" or result.process < 1 or result.process > 2147483647 then
                operations[operation] = nil
                backend.close():await()
                error(failure("workspace_response_invalid"), 0)
            end

            for key in pairs(result) do
                if key ~= "process" then
                    operations[operation] = nil
                    backend.close():await()
                    error(failure("workspace_response_invalid"), 0)
                end
            end

            operation.id = result.process

            if not backend.available or operation.ended then
                terminate(operation)
                error(failure("workspace_closed"), 0)
            end

            workpane.task(function()
                local polled, problem = pcall(function()
                    while not operation.ended and backend.available do
                        local reply = backend.request("process.read", { process = operation.id, limit = maximumChunkBytes })
                        local chunks = output(reply)

                        if operation.ended or not backend.available then
                            return
                        end

                        if #chunks > 0 then
                            operation.options.onOutput(chunks)
                        end

                        if reply.ended then
                            finish(operation, reply.code, reply.failed)
                            return
                        end

                        if #chunks == 0 then
                            async.sleep(20):await()
                        end
                    end
                end)

                if not polled or not operation.ended then
                    terminate(operation, not polled and problem or nil)
                end
            end)

            return operation
        end)
    end

    adapter.write = function(operation, bytes)
        owned(operation)

        if operation.stopping or type(bytes) ~= "string" or #bytes > maximumInputBytes - operation.bytes or #operation.input >= maximumInputMessages then
            error(failure("remote_input_refused"), 0)
        end

        if #bytes == 0 then
            return
        end

        operation.input[#operation.input + 1] = bytes
        operation.bytes = operation.bytes + #bytes

        if operation.writing then
            return
        end

        operation.writing = true
        workpane.task(function()
            local written, problem = pcall(function()
                while not operation.ended and not operation.stopping and backend.available and #operation.input > 0 do
                    local message = operation.input[1]
                    local offset = 1

                    while offset <= #message and not operation.ended and not operation.stopping and backend.available do
                        local part = message:sub(offset, offset + maximumChunkBytes - 1)
                        backend.request("process.write", { process = operation.id, content = crypto.base64Encode(part) })
                        offset = offset + #part
                    end

                    if operation.ended or operation.stopping or not backend.available then
                        return
                    end

                    table.remove(operation.input, 1)
                    operation.bytes = operation.bytes - #message
                end
            end)

            operation.writing = false

            if not written then
                terminate(operation, problem)
            end
        end)
    end

    adapter.stop = function(operation)
        if type(operation) == "table" and operation.owner == adapter and operation.ended then
            return
        end

        owned(operation)

        if operation.stopping then
            return
        end

        operation.stopping = true
        operation.input = {}
        operation.bytes = 0
        workpane.task(function()
            local stopped, problem = pcall(backend.request, "process.stop", { process = operation.id })

            if not stopped then
                terminate(operation, problem)
            end
        end)
    end

    adapter.shutdown = function()
        local closing = {}

        for operation in pairs(operations) do
            closing[#closing + 1] = operation
        end

        for _, operation in ipairs(closing) do
            finish(operation, -1, true, failure("workspace_closed"))
        end
    end

    return adapter
end

return workspaceProcess
