local codec = include("codec")

local diagnostics = {}

local Reader = {}
Reader.__index = Reader

local maximumLineBytes = 4096
local maximumCount = 1 << 40
local numbers = { exitCode = { -(1 << 31), (1 << 31) - 1 }, httpStatus = { 100, 599 }, maximumCharacters = { 0, maximumCount }, actualCharacters = { 0, maximumCount }, timeoutSeconds = { 1, (1 << 31) - 1 } }
local providerCodes = {
    context_length_exceeded = "ai_input_too_large",
    input_too_large = "ai_input_too_large",
    prompt_too_long = "ai_input_too_large",
    rate_limit_exceeded = "ai_rate_limited",
    rate_limit_error = "ai_rate_limited",
    authentication_error = "ai_authentication_failed",
    invalid_api_key = "ai_authentication_failed",
    insufficient_quota = "ai_quota_exceeded",
    overloaded_error = "ai_provider_unavailable",
    server_error = "ai_provider_unavailable",
}

local function count(value)
    return type(value) == "number" and math.type(value) == "integer" and value >= 0 and value <= maximumCount
end

function diagnostics.safe(failure)
    local code = failure.code
    local safe = { code = type(code) == "string" and #code <= 128 and code:match("^[a-z][a-z0-9_]+$") and code or "ai_run_failed" }
    local cause = type(failure.metadata) == "table" and failure.metadata.causeCode or nil

    if type(cause) == "string" and #cause <= 128 and cause:match("^[a-z][a-z0-9_]+$") then
        safe.causeCode = cause
    end

    for name, bounds in pairs(numbers) do
        local value = type(failure.metadata) == "table" and failure.metadata[name] or nil

        if type(value) == "number" and math.type(value) == "integer" and value >= bounds[1] and value <= bounds[2] then
            safe[name] = value
        end
    end

    return safe
end

function diagnostics.new()
    return setmetatable({ pending = "", skipping = false }, Reader)
end

function diagnostics.provider(value, status)
    local supplied = type(value) == "table" and (value.code or value.type) or nil
    local code = type(supplied) == "string" and providerCodes[supplied] or nil
    code = code or (status == 401 and "ai_authentication_failed" or status == 429 and "ai_rate_limited" or status == 503 and "ai_provider_unavailable" or nil)
    local failure = { code = code or "ai_request_failed", message = "The provider could not complete the request", detail = "", metadata = { httpStatus = status } }

    return failure
end

function Reader:line(line)
    local supplied = line:match("^Error: turn/start:.- data: (%b{})%.?%s*$")

    if supplied == nil then
        return
    end

    local read, value = pcall(codec.decode, supplied)

    if read and type(value) == "table" and value.input_error_code == "input_too_large" and count(value.max_chars) and count(value.actual_chars) and value.actual_chars > value.max_chars then
        self.failure = { code = "ai_input_too_large", metadata = { maximumCharacters = value.max_chars, actualCharacters = value.actual_chars } }
    end
end

function Reader:receive(text)
    local position = 1

    while position <= #text do
        local newline = text:find("\n", position, true)
        local part = text:sub(position, newline ~= nil and newline - 1 or #text)

        if not self.skipping and #self.pending + #part <= maximumLineBytes then
            self.pending = self.pending .. part
        else
            self.pending = ""
            self.skipping = true
        end

        if newline == nil then
            return
        end

        if not self.skipping then
            self:line(self.pending)
        end

        self.pending = ""
        self.skipping = false
        position = newline + 1
    end
end

function Reader:finish()
    if not self.skipping then
        self:line(self.pending)
    end

    self.pending = ""

    return self.failure
end

return diagnostics
