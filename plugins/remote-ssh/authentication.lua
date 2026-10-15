local async = require("async")
local protocol = include("protocol")
local ui = workpane.ui
local text = workpane.i18n.text
local translate = workpane.i18n.translate

local authentication = {}

local Authentication = {}
Authentication.__index = Authentication

local function hostTrust(prompt)
    local question = "Are you sure you want to continue connecting "

    return prompt:find(question .. "%(yes/no/%[fingerprint%]%)%?%s*$") ~= nil or prompt:find(question .. "%(yes/no%)%?%s*$") ~= nil
end

function authentication.new(onReject, savedPassword, unavailable)
    local ready, resolve = async.deferred()
    resolve()

    return setmetatable({ closed = false, reject = onReject, credentials = {}, channels = {}, count = 0, savedPassword = savedPassword, unavailable = unavailable, pending = ready }, Authentication)
end

function Authentication:channel()
    if self.closed or self.count >= 40 then
        error(protocol.failure(self.closed and "ssh_connection_closed" or "ssh_channel_full"), 0)
    end

    local finished, finish = async.deferred()
    local owner = self
    local channel = { closed = false, seen = {}, finished = finished }

    function channel:cancel()
        if self.closed then
            return
        end

        self.closed = true
        self.seen = {}
        finish()

        if owner.promptOwner == self then
            if owner.input ~= nil then
                owner.input:set({ value = "" })
            end

            if owner.dialog ~= nil then
                owner.dialog:close("cancel")
            end
        end
    end

    function channel:close()
        if self.closing ~= nil then
            return self.closing
        end

        self:cancel()
        self.closing = async.promise(function()
            local _, problem = self.listener:close():await()
            owner.channels[self] = nil
            owner.count = owner.count - 1

            if problem ~= nil then
                error(problem, 0)
            end
        end)

        return self.closing
    end

    channel.listener = workpane.process.askpass({ onPrompt = function(prompt, kind)
        return owner:prompt(channel, prompt, kind)
    end, onCancel = function()
        channel:cancel()
    end })

    channel.variables = channel.listener.variables
    self.channels[channel] = true
    self.count = self.count + 1

    return channel
end

function Authentication:prompt(channel, prompt, kind)
    if self.closed or channel.closed then
        return nil
    end

    local previous = self.pending
    local ready, finish = async.deferred()
    self.pending = async.promise(function()
        previous:await()
        ready:await()
    end)

    local operation <close> = setmetatable({}, { __close = function()
        finish()
    end })

    async.race({ previous, channel.finished }):await()

    if self.closed or channel.closed then
        return nil
    end

    local password = kind == "input" and prompt:match("^.+@.+'s password:%s*$") ~= nil
    local reusable = password or kind == "input" and prompt:match("^Enter passphrase for key .+:%s*$") ~= nil
    local repeated = channel.seen[prompt] == true
    channel.seen[prompt] = true

    if reusable and repeated then
        self.credentials[prompt] = nil
    end

    if password and not repeated and self.savedPassword ~= nil then
        local secret = self.savedPassword
        self.savedPassword = nil
        self.credentials[prompt] = secret

        return secret
    end

    if reusable and not repeated and self.credentials[prompt] ~= nil then
        return self.credentials[prompt]
    end

    return self:ask(channel, prompt, kind, reusable)
end

function Authentication:ask(channel, prompt, kind, reusable)
    local content = { ui.label({ text = prompt, wrap = true }) }

    if self.unavailable then
        content[#content + 1] = ui.alert({ text = text("remote-ssh.error.credentials"), tone = "warning" })
    end

    local input
    local problem
    local value
    local trust = hostTrust(prompt)

    if kind == "input" and not trust then
        input = ui.textField({ password = true, value = "", grow = 1 })
        problem = ui.alert({ text = text("remote-ssh.auth.invalid"), tone = "danger", visible = false })
        content[#content + 1] = input
        content[#content + 1] = problem
    end

    local dialog

    local function accept()
        if self.closed or channel.closed then
            return
        end

        if input ~= nil then
            local entered = input:get("value")

            if #entered > 1023 or entered:find("[%z\r\n]") ~= nil then
                problem:set({ visible = true })
                return
            end

            value = entered
            input:set({ value = "" })
        else
            value = (kind == "confirm" or trust) and "yes" or ""
        end

        dialog:close("accept")
    end

    if input ~= nil then
        input:on("submit", accept)
    end

    dialog = workpane.dialogs.custom({ title = translate(trust and "remote-ssh.auth.trust-title" or "remote-ssh.auth.title"), width = 580, defaultButton = input == nil and "accept" or nil, content = ui.column({ spacing = 10 }, content), buttons = {
        { id = "cancel", text = translate("remote-ssh.dialog.cancel") },
        { id = "accept", text = translate(trust and "remote-ssh.auth.trust" or "remote-ssh.auth.continue"), variant = "primary", closes = false },
    }, onButton = function(button)
        if button == "accept" then
            accept()
        end
    end })

    self.promptOwner = channel
    self.dialog = dialog
    self.input = input

    if input ~= nil then
        input:command("focus", {})
    end

    local answer, failure = dialog:await()
    self.promptOwner = nil
    self.dialog = nil
    self.input = nil

    if not self.closed and not channel.closed and failure == nil and answer == "cancel" and self.reject ~= nil then
        self.reject()
    end

    if self.closed or channel.closed or failure ~= nil or answer ~= "accept" then
        return nil
    end

    if reusable then
        self.credentials[prompt] = value
    end

    return value
end

function Authentication:close()
    if self.closing ~= nil then
        return self.closing
    end

    self.closed = true
    self.credentials = {}
    self.savedPassword = nil
    local closing = {}

    for channel in pairs(self.channels) do
        closing[#closing + 1] = channel:close()
    end

    self.closing = async.promise(function()
        local outcomes = async.allSettled(closing):await()
        self.pending:await()

        for _, outcome in ipairs(outcomes) do
            if not outcome.ok then
                error(protocol.failure("ssh_cleanup_failed"), 0)
            end
        end
    end)

    return self.closing
end

return authentication
