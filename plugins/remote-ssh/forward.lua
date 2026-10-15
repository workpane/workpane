local async = require("async")
local crypto = require("crypto")
local socket = require("socket")
local protocol = include("protocol")
local proxy = include("proxy")

local forward = {}
local Forward = {}
Forward.__index = Forward

local function raised(code)
    error(protocol.failure(code), 0)
end

local function closeSocket(value)
    return value:close()
end

function forward.new(session, remotePort, localPort, changed)
    if session.closed or session.state ~= "connected" or session.channel == nil then
        return nil, protocol.failure("ssh_connection_closed")
    end

    if math.type(remotePort) ~= "integer" or remotePort < 1 or remotePort > 65535 or math.type(localPort) ~= "integer" or localPort < 0 or localPort > 65535 then
        return nil, protocol.failure("ssh_forward_invalid")
    end

    local accepted, scope = pcall(workpane.resources.scope)

    if not accepted then
        return nil, protocol.failure(type(scope) == "table" and scope.code == "resources_full" and "ssh_forward_limit" or "ssh_connection_closed")
    end

    return setmetatable({ id = crypto.uuidV4(), session = session, remotePort = remotePort, localPort = localPort, changed = changed, scope = scope, closed = false, peers = {}, count = 0 }, Forward)
end

function Forward:serve(client)
    local peers = { client }
    local closing

    local function finish()
        if closing == nil then
            closing = workpane.future(function()
                local pending = {}

                if not self.closed then
                    for _, peer in ipairs(peers) do
                        pending[#pending + 1] = self.scope:release(peer)
                    end
                end

                for _, current in ipairs(pending) do
                    workpane.await(current)
                end

                return {}
            end)
        end

        return closing
    end

    local accepted, problem = pcall(function()
        local peer = workpane.await(self.scope:acquire(function()
            return socket.tcp.connect("127.0.0.1", self.session.channel.port, { timeoutMs = 5000, maxQueuedBytes = 131072 })
        end, closeSocket))
        peers[#peers + 1] = peer
        local handshake = self.scope:run(function()
            local accepted, failure = pcall(proxy.negotiate, peer, self.remotePort)

            return accepted and {} or { failure = failure }
        end)

        local negotiated, failure = async.timeout(handshake, 5000):await()

        if failure ~= nil then
            workpane.await(finish())
            handshake:await()
            raised("ssh_forward_failed")
        end

        if negotiated.failure ~= nil then
            error(negotiated.failure, 0)
        end

        local function pump(source, target)
            return self.scope:run(function()
                local completed, failure = pcall(function()
                    while not self.closed do
                        local bytes = workpane.await(source:receive(65536))

                        if bytes == "" then
                            workpane.await(target:shutdown("send"))
                            return {}
                        end

                        workpane.await(target:send(bytes))
                    end

                    return {}
                end)

                if not completed then
                    workpane.await(finish())
                    return { failure = failure }
                end

                return {}
            end)
        end

        local outgoing = pump(client, peer)
        local incoming = pump(peer, client)
        local first = workpane.await(outgoing)
        local second = workpane.await(incoming)

        if first.failure ~= nil or second.failure ~= nil then
            raised("ssh_forward_failed")
        end
    end)

    workpane.await(finish())
    self.peers[client] = nil
    self.count = self.count - 1

    if not accepted and not self.closed then
        self.problem = protocol.failure(type(problem) == "table" and problem.code == "ssh_proxy_refused" and "ssh_forward_refused" or "ssh_forward_failed")
        self.changed()
    end
end

function Forward:start()
    self.starting = workpane.future(function()
        local accepted, value = pcall(function()
            local listener = workpane.await(self.scope:acquire(function()
                return socket.tcp.listen("127.0.0.1", self.localPort, { backlog = 16, maxQueuedBytes = 131072 })
            end, closeSocket))
            self.localPort = listener.port
            self.changed()
            self.scope:run(function()
                while not self.closed do
                    local client, failure = self.scope:acquire(function() return listener:accept() end, closeSocket):await()
                    if failure ~= nil then
                        if not self.closed then
                            self.problem = protocol.failure("ssh_forward_failed")
                            self.changed()
                        end

                        return {}
                    end

                    if self.count >= 16 then
                        workpane.await(self.scope:release(client))
                    else
                        self.peers[client] = true
                        self.count = self.count + 1
                        self.scope:run(function()
                            self:serve(client)

                            return {}
                        end)
                    end
                end

                return {}
            end)

            return { id = self.id, localPort = self.localPort, remotePort = self.remotePort }
        end)

        if not accepted then
            self:close():await()
            return { failure = protocol.failure(self.session.closed and "ssh_connection_closed" or "ssh_forward_bind_failed") }
        end

        return { value = value }
    end)

    return self.starting
end

function Forward:close()
    if self.closing ~= nil then
        return self.closing
    end

    self.closed = true
    self.closing = self.scope:close()
    self.changed()

    return self.closing
end

return forward
