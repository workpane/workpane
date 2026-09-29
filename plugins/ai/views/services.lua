-- The dialog that adds or edits an MCP server, which is either a local program started with its arguments in a directory or an address.
local catalog = include("catalog")
local engine = include("engine")
local preferences = include("preferences")
local reveal = include("views/reveal")

local ui = workpane.ui
local text = workpane.i18n.text
local translate = workpane.i18n.translate

local servicesView = {}

-- Arguments and roots are written one per line, so an argument holding a space is kept whole.
local function lines(value)
    local list = {}

    for line in ((value or "") .. "\n"):gmatch("(.-)\n") do
        local trimmed = line:match("^%s*(.-)%s*$")

        if trimmed ~= "" then
            list[#list + 1] = trimmed
        end
    end

    return list
end

-- A server is either a local program started with its arguments in a directory or an address, each with the fields only it uses.
function servicesView.serverDialog(existing)
    local fields = {
        id = ui.textField({ value = existing ~= nil and existing.id or "", placeholder = text("ai.mcp.identifier-placeholder"), monospace = true }),
        transport = ui.combo({ value = existing ~= nil and existing.transport or "stdio", sorted = true, options = { { value = "stdio", text = text("ai.mcp.transport-stdio") }, { value = "http", text = text("ai.mcp.transport-http") } } }),
        command = ui.textField({ value = existing ~= nil and existing.command or "", monospace = true }),
        arguments = ui.textArea({ value = existing ~= nil and table.concat(existing.arguments, "\n") or "", placeholder = text("ai.mcp.arguments-placeholder"), rows = 3 }),
        workdir = ui.textField({ value = existing ~= nil and existing.workdir or "" }),
        url = ui.textField({ value = existing ~= nil and existing.url or "" }),
        apiKey = ui.secretField({ value = existing ~= nil and existing.apiKey or "", placeholder = text("ai.settings.api-key-placeholder"), confirmReveal = true }),
        roots = ui.textArea({ value = existing ~= nil and table.concat(existing.roots, "\n") or "", placeholder = text("ai.mcp.roots-placeholder"), rows = 3 }),
        sampling = ui.checkbox({ text = text("ai.mcp.sampling"), checked = existing ~= nil and existing.samplingEnabled or false }),
        samplingTokens = ui.numberField({ value = existing ~= nil and existing.samplingMaximumTokens or 4096, minimum = 0, maximum = catalog.limit("maximumSamplingTokens"), step = 1, decimals = 0, width = 160 }),
    }
    local problem = ui.alert({ text = "", visible = false })
    local dialog

    local function refresh()
        local started = fields.transport:get("value") == "stdio"

        for _, name in ipairs({ "command", "arguments", "workdir" }) do
            fields[name]:set({ enabled = started })
        end

        for _, name in ipairs({ "url", "apiKey" }) do
            fields[name]:set({ enabled = not started })
        end
    end

    fields.transport:on("change", refresh)
    reveal.guard(fields.apiKey)
    refresh()

    local function save()
        local server = { id = (fields.id:get("value") or ""):match("^%s*(.-)%s*$"), transport = fields.transport:get("value"), command = (fields.command:get("value") or ""):match("^%s*(.-)%s*$"), arguments = lines(fields.arguments:get("value")), workdir = (fields.workdir:get("value") or ""):match("^%s*(.-)%s*$"), url = (fields.url:get("value") or ""):match("^%s*(.-)%s*$"), apiKey = fields.apiKey:get("value") or "", roots = lines(fields.roots:get("value")), samplingEnabled = fields.sampling:get("checked") == true, samplingMaximumTokens = math.tointeger(fields.samplingTokens:get("value")) or 0 }
        local list = {}
        local key

        if not server.id:match("^[a-z][a-z0-9-]*$") or #server.id > 32 then
            key = "ai.validation.mcp-identifier"
        elseif server.transport == "stdio" and server.command == "" then
            key = "ai.validation.mcp-command"
        elseif server.transport == "http" and not server.url:match("^https?://[^/%s]+") then
            key = "ai.validation.mcp-address"
        end

        for _, root in ipairs(server.roots) do
            key = key or (not workpane.files.absolute(root) and "ai.validation.mcp-root" or nil)
        end

        for _, candidate in ipairs(preferences.mcpServers()) do
            if existing == nil or candidate.id ~= existing.id then
                key = key or (candidate.id == server.id and "ai.validation.mcp-duplicate" or nil)
                list[#list + 1] = candidate
            end
        end

        if key ~= nil then
            problem:set({ text = text(key), visible = true })
            return
        end

        list[#list + 1] = server
        local accepted, future = pcall(preferences.saveServers, list)
        local failure = future

        if accepted then
            failure = select(2, future:await())
        end

        -- The servers restart and the dialog closes only once the list was kept, and a write that failed stays in the dialog.
        if failure ~= nil then
            workpane.log.error("ai.settings", type(failure) == "table" and tostring(failure.message) or tostring(failure), { code = type(failure) == "table" and failure.code or "", detail = type(failure) == "table" and failure.detail or "" })
            problem:set({ text = text("ai.error.mcp-save"), visible = true })
            return
        end

        engine.restartServers()
        dialog:close("save")
    end

    local content = ui.column({ spacing = 12 }, {
        ui.formField({ label = text("ai.mcp.identifier") }, fields.id),
        ui.formField({ label = text("ai.mcp.transport") }, fields.transport),
        ui.formField({ label = text("ai.mcp.command") }, fields.command),
        ui.formField({ label = text("ai.mcp.arguments") }, fields.arguments),
        ui.formField({ label = text("ai.mcp.workdir") }, fields.workdir),
        ui.formField({ label = text("ai.mcp.address") }, fields.url),
        ui.formField({ label = text("ai.mcp.api-key") }, fields.apiKey),
        ui.formField({ label = text("ai.mcp.roots") }, fields.roots),
        fields.sampling,
        ui.formField({ label = text("ai.mcp.sampling-tokens") }, fields.samplingTokens),
        problem,
    })

    dialog = workpane.dialogs.custom({ title = translate(existing ~= nil and "ai.mcp.edit" or "ai.mcp.add"), width = 620, content = content, buttons = {
        { id = "cancel", text = translate("ai.dialog.cancel") },
        { id = "save", text = translate("ai.dialog.save"), variant = "primary", closes = false },
    }, onButton = function(button)
        if button == "save" then
            save()
        end
    end })

    workpane.await(dialog)
end

return servicesView
