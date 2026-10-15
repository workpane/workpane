local crypto = require("crypto")
local preferences = include("preferences")
local picker = include("folder-picker")
local diagnostics = include("diagnostics")
local credentials = include("credentials")

local ui = workpane.ui
local text = workpane.i18n.text
local translate = workpane.i18n.translate

local form = {}

local errors = {
    ssh_profile_invalid = "remote-ssh.error.profile",
    ssh_profile_name_invalid = "remote-ssh.error.name",
    ssh_profile_host_invalid = "remote-ssh.error.host",
    ssh_profile_user_invalid = "remote-ssh.error.user",
    ssh_profile_port_invalid = "remote-ssh.error.port",
    ssh_profile_path_invalid = "remote-ssh.error.path",
    ssh_profile_root_invalid = "remote-ssh.error.root",
    ssh_profile_timeout_invalid = "remote-ssh.error.timeout",
    ssh_profile_limit = "remote-ssh.error.limit",
    ssh_password_required = "remote-ssh.error.password-required",
    credential_request_invalid = "remote-ssh.auth.invalid",
    credential_store_unavailable = "remote-ssh.error.credentials",
    credential_store_locked = "remote-ssh.error.credentials",
    ssh_credential_cleanup_failed = "remote-ssh.error.credentials",
}

function form.open(id)
    local profile = id ~= nil and preferences.find(id) or nil
    local values = profile or { id = crypto.uuidV4(), name = "", host = "", user = "", port = 0, identity = "", configuration = "", root = "", timeoutSeconds = preferences.defaultTimeoutSeconds, savePassword = false }
    local savedPassword, credentialFailure

    if profile ~= nil and profile.savePassword then
        savedPassword, credentialFailure = credentials.read(profile)
    end

    local password
    local savePassword
    local inputs = {}
    local dialog
    local busy = false
    local selecting = false
    local selectionScope
    local operationStatus
    local operationLabel
    local operationCancel
    local problem

    local function buttons()
        return {
            { id = "cancel", text = translate("remote-ssh.dialog.cancel"), enabled = not busy },
            { id = "save", text = translate("remote-ssh.dialog.save"), variant = "primary", closes = false, enabled = not busy and not selecting },
        }
    end

    local function lockSelection(locked)
        selecting = locked
        operationStatus:set({ visible = locked or busy })
        operationLabel:set({ text = text(busy and "remote-ssh.dialog.saving" or "remote-ssh.picker.connecting") })
        operationCancel:set({ visible = locked })

        for field, input in pairs(inputs) do
            input:set({ enabled = not busy and (field == "root" or not locked) })
        end

        password:set({ enabled = not busy and not locked })
        savePassword:set({ enabled = not busy and not locked and credentialFailure == nil })
        dialog:setButtons(buttons())
    end

    for _, field in ipairs({ "name", "host", "user" }) do
        inputs[field] = ui.textField({ value = values[field], grow = 1 })
    end

    for _, field in ipairs({ "identity", "configuration" }) do
        inputs[field] = ui.pathField({ value = values[field], mode = "file", grow = 1, pickerTitle = text("remote-ssh.field." .. field) })
    end

    inputs.root = ui.pathField({ value = values.root, mode = "folder", placeholder = text("remote-ssh.picker.home"), grow = 1, onBrowse = function(event)
        local profile = { id = values.id, savePassword = values.savePassword }

        for key, input in pairs(inputs) do
            profile[key] = input:get("value")
        end

        profile.port = math.tointeger(profile.port)
        profile.timeoutSeconds = math.tointeger(profile.timeoutSeconds)

        problem:set({ visible = false })
        lockSelection(true)
        local accepted, chosen = pcall(picker.profile, profile, event.isCurrent, function(scope) selectionScope = scope end, password:get("value"))
        selectionScope = nil

        if inputs.root:mounted() then
            lockSelection(false)
        end

        if not accepted then
            if inputs.root:mounted() and event.isCurrent() then
                local code = type(chosen) == "table" and chosen.code or "ssh_connection_failed"
                problem:set({ text = text(errors[code] or diagnostics.key({ code = code })), visible = true })
            end

            return nil
        end

        return chosen
    end })

    inputs.port = ui.numberField({ value = values.port, minimum = 0, maximum = 65535, decimals = 0, step = 1, grow = 1 })
    inputs.timeoutSeconds = ui.numberField({ value = values.timeoutSeconds, minimum = 5, maximum = 300, decimals = 0, step = 1, grow = 1 })

    password = ui.textField({ password = true, value = "", placeholder = text(savedPassword ~= nil and "remote-ssh.password.saved" or "remote-ssh.password.placeholder"), grow = 1 })
    savePassword = ui.checkbox({ checked = values.savePassword, enabled = credentialFailure == nil, text = text("remote-ssh.password.save") })
    savedPassword = nil

    problem = ui.alert({ text = credentialFailure ~= nil and text("remote-ssh.error.credentials") or "", tone = "danger", visible = credentialFailure ~= nil })
    operationLabel = ui.label({ text = "", grow = 1 })
    operationCancel = ui.button({ text = text("remote-ssh.dialog.cancel"), onClick = function()
        if selectionScope ~= nil then
            workpane.await(selectionScope:close())
        end
    end })

    operationStatus = ui.row({ spacing = 8, visible = false }, { ui.busyIndicator({ running = true, size = 16 }), operationLabel, operationCancel })
    local content = {}

    for _, field in ipairs({ "name", "host", "user" }) do
        content[#content + 1] = ui.formField({ label = text("remote-ssh.field." .. field:lower()) }, inputs[field])
    end

    content[#content + 1] = ui.formField({ label = text("remote-ssh.password.label") }, password)
    content[#content + 1] = savePassword

    for _, field in ipairs({ "port", "identity", "configuration", "root", "timeoutSeconds" }) do
        content[#content + 1] = ui.formField({ label = text("remote-ssh.field." .. field:lower()) }, inputs[field])
    end

    content[#content + 1] = ui.label({ text = text("remote-ssh.dialog.configuration"), wrap = true, style = "muted" })
    content[#content + 1] = operationStatus
    content[#content + 1] = problem
    dialog = workpane.dialogs.custom({ title = translate(profile ~= nil and "remote-ssh.dialog.edit" or "remote-ssh.dialog.new"), width = 580, defaultButton = "save", content = ui.column({ spacing = 8 }, content), buttons = buttons(), onButton = function(button)
        if button ~= "save" or busy or selecting then
            return
        end

        busy = true
        lockSelection(selecting)
        local edited = { id = values.id, savePassword = savePassword:get("checked") }

        for field, input in pairs(inputs) do
            edited[field] = input:get("value")
        end

        edited.port = math.tointeger(edited.port)
        edited.timeoutSeconds = math.tointeger(edited.timeoutSeconds)
        local saved, failure = credentials.save(edited, password:get("value"), profile)

        busy = false

        if not inputs.root:mounted() then
            return
        end

        if saved then
            password:set({ value = "" })
            dialog:close("saved")
            return
        end

        problem:set({ text = text(errors[failure.code] or "remote-ssh.error.save"), visible = true })
        lockSelection(selecting)
    end })

    inputs.root:on("change", function()
        if selectionScope ~= nil then
            workpane.await(selectionScope:close())
        end
    end)

    dialog:await()
    password:set({ value = "" })

    if selectionScope ~= nil then
        workpane.await(selectionScope:close())
    end
end

return form
