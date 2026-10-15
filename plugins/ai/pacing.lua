-- Paces the requests of every task per provider: a minimum interval, a number per minute and a number in flight, admitted first come first served.
local async = require("async")
local datetime = require("datetime")
local preferences = include("preferences")

local pacing = {}

local Ticket = {}
Ticket.__index = Ticket

local windowSeconds = 60
local pollMilliseconds = 25

local states = {}

local function state(providerId)
    if states[providerId] == nil then
        states[providerId] = { waiters = {}, held = 0, admissions = {}, last = nil }
    end

    return states[providerId]
end

-- A zero on any limit means the provider was never told to wait for that one.
local function delay(provider, limit, now)
    local wait = 0

    if limit.minimumIntervalMs > 0 and provider.last ~= nil then
        wait = math.max(wait, provider.last + limit.minimumIntervalMs / 1000 - now)
    end

    while provider.admissions[1] ~= nil and provider.admissions[1] + windowSeconds <= now do
        table.remove(provider.admissions, 1)
    end

    if limit.maximumRequestsPerMinute > 0 and #provider.admissions >= limit.maximumRequestsPerMinute then
        wait = math.max(wait, provider.admissions[#provider.admissions - limit.maximumRequestsPerMinute + 1] + windowSeconds - now)
    end

    return math.max(0, wait)
end

local function full(provider, limit)
    return limit.maximumConcurrentRequests > 0 and provider.held >= limit.maximumConcurrentRequests
end

function pacing.ticket(providerId)
    return setmetatable({ providerId = providerId, admitted = false, withdrawn = false }, Ticket)
end

-- Waits for its turn and reports how long the first wait is expected to be, which is zero when only the concurrency is full.
function Ticket:wait(throttled)
    local provider = state(self.providerId)
    provider.waiters[#provider.waiters + 1] = self
    local reported = false

    while not self.withdrawn do
        local limit = preferences.rateLimit(self.providerId)
        local now = datetime.now():millis() / 1000
        local busy = full(provider, limit)
        local wait = busy and 0 or delay(provider, limit, now)

        if provider.waiters[1] == self and not busy and wait == 0 then
            table.remove(provider.waiters, 1)
            provider.held = provider.held + 1
            provider.last = now
            provider.admissions[#provider.admissions + 1] = now
            self.admitted = true
            return true
        end

        if not reported and (busy or wait > 0 or provider.waiters[1] ~= self) then
            reported = true
            throttled(math.floor(wait * 1000))
        end

        async.sleep(pollMilliseconds):await()
    end

    return false
end

-- A place is given back the moment its holder stops wanting it, whether it was still waiting for that place or already holding it.
function Ticket:release()
    local provider = state(self.providerId)

    if self.admitted then
        self.admitted = false
        provider.held = provider.held - 1
    end

    if not self.withdrawn then
        self.withdrawn = true

        for index, waiter in ipairs(provider.waiters) do
            if waiter == self then
                table.remove(provider.waiters, index)
                break
            end
        end
    end
end

return pacing
