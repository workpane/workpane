-- Holds one turn of a conversation with a provider over HTTP: the request body of its protocol, the streamed answer, the retries and the rate limits of the provider.
local async = require("async")
local datetime = require("datetime")
local http = require("http")
local catalog = include("catalog")
local codec = include("codec")
local connections = include("connections")
local cron = include("cron")
local pacing = include("pacing")
local protocols = include("protocols")

local chat = {}

local Client = {}
Client.__index = Client

local maximumBufferBytes = 1 << 20
local maximumContentBytes = 1 << 22
local maximumFailureBytes = 1 << 16
local maximumReasonCharacters = 500
local pollMilliseconds = 20
local months = { Jan = 1, Feb = 2, Mar = 3, Apr = 4, May = 5, Jun = 6, Jul = 7, Aug = 8, Sep = 9, Oct = 10, Nov = 11, Dec = 12 }

local function now()
    return datetime.now():millis()
end

local function refuse(code, message, detail)
    error({ code = code, message = message, detail = detail or "" }, 0)
end

-- A model that ran out of answer budget stopped mid sentence, which every protocol reports as length once its reason is read.
function chat.truncated(finishReason)
    return finishReason == "length"
end

-- The body carries the conversation in the shape of the protocol, the declared parameters at the fields the catalog names and whatever the reader added beyond them.
-- The messages arrive canonical and already fitted to the model, so this is the one place that knows how the protocol writes them.
function chat.body(provider, request, translate)
    local connection = request.connection
    local anthropic = provider.protocol == "anthropic"
    local encoded = protocols.encode(provider.protocol, request.messages, translate)
    local body = { model = connection.modelId, messages = encoded.messages, stream = true }

    if anthropic and encoded.system ~= nil then
        body.system = encoded.system
    elseif not anthropic then
        body.stream_options = { include_usage = true }
    end

    for _, parameter in ipairs(catalog.parameters(provider, connection.modelId)) do
        local value = connection.parameters[parameter.id]

        if value ~= nil and parameter.modelMaximumWhenZero and value == 0 then
            local maximum = connections.outputBudget(connection)

            if maximum > 0 then
                connections.applyField(body, parameter.field, maximum)
            end
        elseif value ~= nil then
            connections.applyField(body, parameter.field, value)
        end
    end

    local extras = connections.extras(connection.extraParameters)
    local keys = {}

    for key in pairs(extras) do
        keys[#keys + 1] = key
    end

    table.sort(keys)

    for _, key in ipairs(keys) do
        connections.applyField(body, key, extras[key])
    end

    if request.tools ~= nil and #request.tools > 0 then
        body.tools = protocols.serializeTools(provider.protocol, request.tools, translate)
    end

    return body
end

-- Services report a rejection in more than one shape, and one reported in none is quoted so the reason is still readable.
function chat.providerMessage(body)
    local document = codec.read(body)
    local plain = body:gsub("%s+", " "):match("^%s*(.-)%s*$"):sub(1, maximumReasonCharacters)

    if type(document) ~= "table" then
        return plain
    end

    local nested = type(document.error) == "table" and document.error or {}
    local kind = type(nested.type) == "string" and nested.type or (type(document.type) == "string" and document.type or "")
    local message = type(nested.message) == "string" and nested.message or nil
    message = message or (type(document.error) == "string" and document.error or nil)
    message = message or (type(document.message) == "string" and document.message or nil)
    message = message or (type(document.detail) == "string" and document.detail or nil)

    if message == nil or message == "" then
        return plain
    end

    return kind ~= "" and (message .. " (type \"" .. kind .. "\")") or message
end

local function rfcMoment(text)
    local day, month, year, hour, minute, second = text:match("^%a+, (%d+) (%a+) (%d+) (%d+):(%d+):(%d+) GMT$")

    if day == nil or months[month] == nil then
        return nil
    end

    return cron.epoch(string.format("%04d-%02d-%02dT%02d:%02d:%02d", tonumber(year), months[month], tonumber(day), tonumber(hour), tonumber(minute), tonumber(second)))
end

-- A service that says how long to wait is obeyed within the ceiling, and otherwise the wait doubles with every attempt up to it.
function chat.retryDelay(retryAfter, attempt)
    local ceiling = catalog.limit("maximumRetryBackoffMs")
    local text = type(retryAfter) == "string" and retryAfter:match("^%s*(.-)%s*$") or ""
    local seconds = text:match("^%d+$") and tonumber(text) or nil

    if seconds ~= nil then
        return math.min(ceiling, math.min(seconds, ceiling) * 1000)
    end

    local moment = rfcMoment(text)

    if moment ~= nil then
        return math.max(0, math.min(ceiling, moment * 1000 - now()))
    end

    return math.min(ceiling, catalog.limit("retryBackoffMs") << attempt)
end

-- Only a transient condition is tried again, because a rejected request repeats the same rejection.
function chat.retryable(status)
    return status == nil or status == 408 or status == 429 or status >= 500
end

function chat.new(handlers)
    return setmetatable({ handlers = handlers, cancelled = false }, Client)
end

function Client:cancel()
    self.cancelled = true

    if self.ticket ~= nil then
        self.ticket:release()
    end
end

-- One attempt streams its answer while this coroutine watches for the end, the idle limit and a cancellation, and a stream it gives up on is left to finish unheard.
function Client:attempt(provider, request, apiKey, translate)
    local endpoint = connections.endpoint(provider.id, request.address, "chat")

    if endpoint == nil then
        refuse("ai_provider_unknown", translate("ai.error.provider-unknown"), provider.id)
    end

    local query = {}

    for key, value in pairs(provider.queryParameters) do
        query[#query + 1] = http.urlEncode(key) .. "=" .. http.urlEncode(value)
    end

    table.sort(query)

    if #query > 0 then
        endpoint = endpoint .. (endpoint:find("?", 1, true) and "&" or "?") .. table.concat(query, "&")
    end

    local headers = { ["Content-Type"] = "application/json", accept = "text/event-stream" }

    for key, value in pairs(provider.headers) do
        headers[key] = value
    end

    if provider.protocol == "anthropic" then
        headers["x-api-key"] = apiKey
    elseif apiKey ~= "" then
        headers.authorization = "Bearer " .. apiKey
    end

    local payload = codec.encode(chat.body(provider, request, translate))
    local state = { buffer = "", failure = {}, failureBytes = 0, content = {}, contentBytes = 0, thoughts = {}, thoughtAt = {}, usage = { input = 0, output = 0 }, finish = "", calls = protocols.accumulator(provider.protocol), activity = now() }

    local function stop(failure)
        state.problem = state.problem or failure
    end

    -- Every block of reasoning is kept apart, in the order the model wrote them, with the signature or the redacted data that proves it.
    local function thought(index)
        if state.thoughtAt[index] == nil then
            state.thoughts[#state.thoughts + 1] = { texts = {} }
            state.thoughtAt[index] = state.thoughts[#state.thoughts]
        end

        return state.thoughtAt[index]
    end

    local function consume(data)
        if data == "" or data == "[DONE]" then
            return
        end

        local event = codec.read(data)

        if type(event) ~= "table" then
            stop({ code = "ai_stream_invalid", message = "The provider returned an invalid stream event", detail = "" })
            return
        end

        state.calls:consume(event)
        local delta

        if provider.protocol == "anthropic" then
            local change = type(event.delta) == "table" and event.delta or {}

            local block = type(event.content_block) == "table" and event.content_block or {}

            if event.type == "content_block_delta" and type(change.text) == "string" then
                delta = change.text
            elseif event.type == "content_block_start" and block.type == "redacted_thinking" and type(block.data) == "string" then
                thought(event.index).redacted = block.data
            elseif event.type == "content_block_delta" and type(change.thinking) == "string" then
                local texts = thought(event.index).texts
                texts[#texts + 1] = change.thinking
            elseif event.type == "content_block_delta" and type(change.signature) == "string" then
                thought(event.index).signature = change.signature
            elseif event.type == "message_start" and type(event.message) == "table" and type(event.message.usage) == "table" then
                state.usage.input = math.tointeger(event.message.usage.input_tokens) or state.usage.input
            elseif event.type == "message_delta" then
                state.usage.output = type(event.usage) == "table" and math.tointeger(event.usage.output_tokens) or state.usage.output
                state.finish = type(event.delta) == "table" and type(event.delta.stop_reason) == "string" and event.delta.stop_reason or state.finish
            elseif event.type == "error" then
                stop({ code = "ai_provider_error", message = type(event.error) == "table" and event.error.message or "The provider reported an error", detail = "" })
            end
        else
            local choice = type(event.choices) == "table" and event.choices[1] or nil

            if type(choice) == "table" then
                local change = type(choice.delta) == "table" and choice.delta or {}
                local reasoning = type(change.reasoning_content) == "string" and change.reasoning_content or (type(change.reasoning) == "string" and change.reasoning or nil)
                delta = type(change.content) == "string" and change.content or nil

                if reasoning ~= nil then
                    local texts = thought(0).texts
                    texts[#texts + 1] = reasoning
                end

                state.finish = type(choice.finish_reason) == "string" and choice.finish_reason or state.finish
            end

            if type(event.usage) == "table" then
                state.usage.input = math.tointeger(event.usage.prompt_tokens) or state.usage.input
                state.usage.output = math.tointeger(event.usage.completion_tokens) or state.usage.output
            end

            if type(event.error) == "table" then
                stop({ code = "ai_provider_error", message = event.error.message or "The provider reported an error", detail = "" })
            end
        end

        if delta ~= nil and delta ~= "" and state.problem == nil then
            state.contentBytes = state.contentBytes + #delta

            if state.contentBytes > maximumContentBytes then
                stop({ code = "ai_stream_too_large", message = "The provider stream exceeded the permitted size", detail = "" })
                return
            end

            state.content[#state.content + 1] = delta
            self.handlers.content(delta)
        end
    end

    local function received(chunk)
        if state.problem ~= nil or self.cancelled then
            return
        end

        state.activity = now()

        if state.failureBytes < maximumFailureBytes then
            state.failure[#state.failure + 1] = chunk:sub(1, maximumFailureBytes - state.failureBytes)
            state.failureBytes = state.failureBytes + #chunk
        end

        if state.status ~= nil and (state.status < 200 or state.status >= 300) then
            return
        end

        state.buffer = state.buffer .. chunk

        if #state.buffer > maximumBufferBytes then
            stop({ code = "ai_stream_too_large", message = "The provider stream exceeded the permitted size", detail = "" })
            return
        end

        -- Lines are cut at each newline found from where the last one ended, so a long line costs its length once instead of its square.
        local start = 1
        local newline = state.buffer:find("\n", start, true)

        while newline ~= nil do
            local trimmed = state.buffer:sub(start, newline - 1):match("^%s*(.-)%s*$")

            if trimmed:sub(1, 5) == "data:" then
                consume(trimmed:sub(6):match("^%s*(.-)%s*$"))
            end

            start = newline + 1
            newline = state.buffer:find("\n", start, true)
        end

        state.buffer = state.buffer:sub(start)
    end

    workpane.task(function()
        local _, failure = http.client.stream({ url = endpoint, method = "POST", headers = headers, body = payload, timeoutSeconds = catalog.limit("requestTimeoutMs") / 1000, onResponse = function(status, responseHeaders)
            state.status = status
            state.retryAfter = type(responseHeaders) == "table" and responseHeaders["retry-after"] or nil
            state.activity = now()
        end }, received):await()
        state.transport = failure
        state.done = true
    end)

    self.handlers.requestSent(endpoint, #payload, #request.messages)

    while not state.done and state.problem == nil and not self.cancelled do
        if now() - state.activity > provider.streamIdleTimeoutMs then
            stop({ code = "ai_stream_idle", message = "The provider stream stopped sending data", detail = "" })
        end

        async.sleep(pollMilliseconds):await()
    end

    if self.cancelled then
        refuse("ai_cancelled", "The request was cancelled", "")
    end

    if state.problem ~= nil then
        return { failure = state.problem }
    end

    if not state.buffer:match("^%s*$") then
        received("\n")
    end

    local failureBody = table.concat(state.failure)
    local ok = state.transport == nil and state.status ~= nil and state.status >= 200 and state.status < 300

    if not ok then
        local reported = failureBody ~= "" and chat.providerMessage(failureBody) or tostring(state.transport or "")
        local statusLine = state.status ~= nil and ("HTTP " .. state.status) or tostring(state.transport or "")
        return { retry = chat.retryable(state.status), retryAfter = state.retryAfter, failure = { code = "ai_request_failed", message = reported ~= "" and reported or statusLine, detail = failureBody ~= "" and (statusLine .. "\n" .. failureBody) or statusLine } }
    end

    if state.problem ~= nil then
        return { failure = state.problem }
    end

    local calls, invalid = state.calls:calls()
    local finish = protocols.finishReason(provider.protocol, state.finish)

    if calls == nil then
        return { failure = chat.truncated(finish) and { code = "ai_output_truncated", message = invalid.message, detail = state.finish } or invalid }
    end

    local reasoning = {}

    for index, block in ipairs(state.thoughts) do
        reasoning[index] = { text = table.concat(block.texts), signature = block.signature, redacted = block.redacted }
    end

    return { result = { content = table.concat(state.content), reasoning = reasoning, calls = calls, usage = state.usage, finishReason = finish } }
end

-- Sends one turn and answers the text, the tool calls, the usage and the finish reason, or raises the failure that ended it.
function Client:send(request, translate)
    local connection = connections.validate(request.connection)
    local provider = catalog.provider(connection.providerId)
    local apiKey = connections.secret(connection.apiKey)
    local retries = 0

    if provider.requiresApiKey and apiKey == "" then
        refuse("ai_api_key_missing", "The provider requires an API key", provider.id)
    end

    request.connection = connection

    while true do
        self.ticket = pacing.ticket(provider.id)
        local admitted = self.ticket:wait(function(milliseconds)
            self.handlers.throttled("rate-limit", milliseconds)
        end)

        -- A request cancelled while it waited gives its place back, whether or not it had been admitted.
        if not admitted or self.cancelled then
            self.ticket:release()
            refuse("ai_cancelled", "The request was cancelled", "")
        end

        local answered, outcome = pcall(self.attempt, self, provider, request, apiKey, translate)
        self.ticket:release()

        if not answered then
            error(outcome, 0)
        end

        if outcome.result ~= nil then
            return outcome.result
        end

        if not outcome.retry or retries >= provider.requestMaxRetries then
            error(outcome.failure, 0)
        end

        local wait = chat.retryDelay(outcome.retryAfter, retries)
        retries = retries + 1
        self.handlers.throttled("retry", wait, outcome.failure.message)
        async.sleep(math.max(1, wait)):await()

        if self.cancelled then
            refuse("ai_cancelled", "The request was cancelled", "")
        end
    end
end

-- The models a wire provider publishes, de-duplicated and sorted, for the connection form to offer beside the catalog.
function chat.discover(connection)
    local provider = catalog.provider(connection.providerId)
    local address = connections.address(connection)
    local apiKey = connections.secret(connection.apiKey)
    local url = address .. (provider.protocol == "anthropic" and "/v1/models" or "/models")
    local headers = { accept = "application/json" }

    for key, value in pairs(provider.headers) do
        headers[key] = value
    end

    if provider.protocol == "anthropic" then
        headers["x-api-key"] = apiKey
    elseif apiKey ~= "" then
        headers.authorization = "Bearer " .. apiKey
    end

    local response, failure = http.client.requestRaw({ url = url, method = "GET", headers = headers, timeoutSeconds = catalog.limit("discoveryTimeoutMs") / 1000, maxResponseBytes = 4 * 1024 * 1024 }):await()

    if failure ~= nil or response.status < 200 or response.status >= 300 then
        refuse("ai_model_discovery_failed", failure ~= nil and tostring(failure) or ("HTTP " .. response.status), response ~= nil and response.body:sub(1, 512) or "")
    end

    local document = codec.read(response.body)
    local seen = {}
    local found = {}

    for _, entry in ipairs(type(document) == "table" and type(document.data) == "table" and document.data or {}) do
        if type(entry) == "table" and type(entry.id) == "string" and entry.id ~= "" and not seen[entry.id] then
            seen[entry.id] = true
            found[#found + 1] = entry.id
        end
    end

    if #found == 0 then
        refuse("ai_model_discovery_empty", "The provider published no model", provider.id)
    end

    table.sort(found)

    return found
end

return chat
