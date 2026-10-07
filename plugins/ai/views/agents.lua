-- The dialog that writes an agent, with its prompt templates and the tags a prompt may carry.
local catalog = include("catalog")
local connections = include("connections")
local engine = include("engine")
local preferences = include("preferences")
local prompt = include("prompt")
local templates = include("templates")

local ui = workpane.ui
local text = workpane.i18n.text
local translate = workpane.i18n.translate

local agentsView = {}

local maximumIdentifier = 48

-- An identifier left empty is spelled from the name: lowercase letters and digits, one hyphen between words, starting with a letter.
function agentsView.identifier(name)
    local spelled = name:lower():gsub("[^a-z0-9]+", "-"):gsub("^[^a-z]+", ""):sub(1, maximumIdentifier):gsub("%-+$", "")
    return spelled
end

local function showTags()
    local lines = {}

    for _, tag in ipairs(prompt.tags()) do
        lines[#lines + 1] = "- `{{" .. tag .. "}}` — " .. translate(prompt.descriptionKey(tag))
    end

    local shown = workpane.dialogs.custom({ title = translate("ai.agent.show-tags"), width = 640, content = ui.scroll({ height = 420 }, ui.markdown({ text = table.concat(lines, "\n") })), buttons = { { id = "close", text = translate("ai.dialog.close") } } })
    workpane.await(shown)
end

-- The first rule a form breaks is named, so the reader fixes one thing at a time.
local function problem(values, existing)
    if not values.id:match("^[a-z][a-z0-9-]*$") or #values.id > maximumIdentifier then
        return text("ai.validation.agent-identifier")
    end

    for _, agent in ipairs(preferences.agents()) do
        if agent.id == values.id and (existing == nil or existing.id ~= values.id) then
            return text("ai.validation.agent-duplicate")
        end
    end

    if values.name:match("^%s*$") then
        return text("ai.validation.agent-name")
    end

    if connections.find(preferences.connections(), values.connectionKey) == nil then
        return text("ai.validation.connection-missing")
    end

    if values.systemPrompt:match("^%s*$") then
        return text("ai.validation.agent-prompt")
    end

    local unknown = prompt.unknownTags(values.systemPrompt)
    return #unknown > 0 and text("ai.validation.agent-tag", table.concat(unknown, ", ")) or nil
end

function agentsView.dialog(existing)
    local connectionOptions = {}

    for index, connection in ipairs(preferences.connections()) do
        connectionOptions[index] = { value = connections.key(connection), text = connections.label(connection) }
    end

    local templateOptions = {}

    for index, template in ipairs(templates.list()) do
        templateOptions[index] = { value = template, text = text("ai.template." .. template .. "-name") }
    end

    local defaultConnection = preferences.defaultConnection()
    local fields = {
        name = ui.textField({ value = existing ~= nil and existing.name or "" }),
        identifier = ui.textField({ value = existing ~= nil and existing.id or "", placeholder = text("ai.agent.identifier-placeholder"), monospace = true }),
        description = ui.textField({ value = existing ~= nil and existing.description or "", placeholder = text("ai.agent.description-placeholder") }),
        connection = ui.combo({ value = existing ~= nil and existing.connectionKey or (defaultConnection ~= nil and connections.key(defaultConnection) or ""), options = connectionOptions, sorted = true }),
        iterations = ui.numberField({ value = existing ~= nil and existing.maximumIterations or 8, minimum = 0, maximum = catalog.limit("maximumAgentIterations"), step = 1, decimals = 0, width = 160, tooltip = text("ai.task.unlimited-hint") }),
        template = ui.combo({ value = templates.list()[1], options = templateOptions, sorted = true, grow = 1 }),
        systemPrompt = ui.textArea({ value = existing ~= nil and existing.systemPrompt or "", placeholder = text("ai.agent.system-prompt-placeholder"), rows = 14 }),
    }
    local problemLabel = ui.alert({ text = "", visible = false })
    local dialog

    -- A template replaces the prompt being written only once the reader confirms it, so a prompt written by hand is never lost to a click.
    local insert = ui.button({ text = text("ai.agent.insert-template"), onClick = function()
        local written = fields.systemPrompt:get("value") or ""
        local replaced = written:match("%S") == nil or workpane.await(workpane.dialogs.confirm({ title = translate("ai.agent.insert-template"), message = translate("ai.agent.replace-prompt-message"), detail = translate("ai.template." .. fields.template:get("value") .. "-name"), confirmText = translate("ai.agent.replace-prompt"), destructive = true }))

        if replaced then
            fields.systemPrompt:set({ value = templates.body(fields.template:get("value")) })
        end
    end })

    local tags = ui.button({ text = text("ai.agent.show-tags"), onClick = showTags })
    local promptRow = ui.formField({ label = text("ai.agent.system-prompt"), hint = text("ai.template." .. fields.template:get("value") .. "-description") }, ui.row({ spacing = 8 }, { fields.template, insert, tags }))

    -- The template chosen is described under it before the reader inserts it.
    fields.template:on("change", function(event)
        promptRow:set({ hint = text("ai.template." .. event.value .. "-description") })
    end)

    local function save()
        local name = fields.name:get("value") or ""
        local identifier = (fields.identifier:get("value") or ""):match("^%s*(.-)%s*$")
        local values = { id = identifier ~= "" and identifier or agentsView.identifier(name), name = name, description = fields.description:get("value") or "", connectionKey = fields.connection:get("value") or "", maximumIterations = math.tointeger(fields.iterations:get("value")) or 0, systemPrompt = fields.systemPrompt:get("value") or "" }
        local refused = problem(values, existing)

        if refused ~= nil then
            problemLabel:set({ text = refused, visible = true })
            return
        end

        -- An edited agent keeps its place in the list, and a new one joins at its end.
        local list = {}

        for _, agent in ipairs(preferences.agents()) do
            list[#list + 1] = existing ~= nil and agent.id == existing.id and values or agent
        end

        if existing == nil then
            list[#list + 1] = values
        end

        local accepted, future = pcall(preferences.saveAgents, list)
        local failure = future

        if accepted then
            failure = select(2, future:await())
        end

        -- The tasks of a changed agent stop and the dialog closes only once the agents were kept, and a write that failed stays in the dialog.
        if failure ~= nil then
            workpane.log.error("ai.settings", type(failure) == "table" and tostring(failure.message) or tostring(failure), { code = type(failure) == "table" and failure.code or "", detail = type(failure) == "table" and tostring(failure.detail) or "" })
            problemLabel:set({ text = text("ai.error.agent-save"), visible = true })
            return
        end

        -- A changed identifier leaves the tasks of the former one without an agent, so they stop as they would if it had been removed.
        engine.stopOrphans()
        dialog:close("save")
    end

    local content = ui.column({ spacing = 12 }, {
        ui.formField({ label = text("ai.agent.name") }, fields.name),
        ui.formField({ label = text("ai.agent.identifier"), hint = text("ai.agent.identifier-hint") }, fields.identifier),
        ui.formField({ label = text("ai.agent.description") }, fields.description),
        ui.formField({ label = text("ai.agent.connection") }, fields.connection),
        ui.formField({ label = text("ai.agent.maximum-iterations"), hint = text("ai.task.unlimited-hint") }, fields.iterations),
        promptRow,
        fields.systemPrompt,
        problemLabel,
    })

    dialog = workpane.dialogs.custom({ title = translate(existing ~= nil and "ai.agent.edit" or "ai.agent.add"), width = 720, content = content, buttons = {
        { id = "cancel", text = translate("ai.dialog.cancel") },
        { id = "save", text = translate("ai.dialog.save"), variant = "primary", closes = false },
    }, onButton = function(button)
        if button == "save" then
            save()
        end
    end })

    workpane.await(dialog)
end

return agentsView
