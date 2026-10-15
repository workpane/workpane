local protocol = include("protocol")

local proxy = {}

local function raised(code)
    error(protocol.failure(code), 0)
end

local function awaited(promise)
    local value, failure = promise:await()

    if failure ~= nil then
        raised("ssh_transport_failed")
    end

    return value
end

function proxy.readExactly(peer, count)
    local chunks = {}
    local received = 0

    while received < count do
        local bytes = awaited(peer:receive(math.min(count - received, 65536)))

        if bytes == "" then
            raised("ssh_transport_closed")
        end

        chunks[#chunks + 1] = bytes
        received = received + #bytes
    end

    return table.concat(chunks)
end

function proxy.negotiate(peer, port)
    awaited(peer:send("\5\1\0"))

    if proxy.readExactly(peer, 2) ~= "\5\0" then
        raised("ssh_proxy_invalid")
    end

    awaited(peer:send("\5\1\0\1\127\0\0\1" .. string.pack(">I2", port)))
    local reply = proxy.readExactly(peer, 4)

    if reply:byte(1) ~= 5 or reply:byte(3) ~= 0 then
        raised("ssh_proxy_invalid")
    end

    if reply:byte(2) ~= 0 then
        raised("ssh_proxy_refused")
    end

    local kind = reply:byte(4)

    if kind == 1 then
        proxy.readExactly(peer, 6)
    elseif kind == 4 then
        proxy.readExactly(peer, 18)
    elseif kind == 3 then
        local length = proxy.readExactly(peer, 1):byte(1)

        if length == 0 then
            raised("ssh_proxy_invalid")
        end

        proxy.readExactly(peer, length + 2)
    else
        raised("ssh_proxy_invalid")
    end
end

return proxy
