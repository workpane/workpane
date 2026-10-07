-- The dialog that adds or edits a model connection, with the parameters of its model and the extra parameters of its provider.
local catalog = include("catalog")
local chat = include("chat")
local codec = include("codec")
local connections = include("connections")
local preferences = include("preferences")
local reveal = include("views/reveal")

local ui = workpane.ui
local text = workpane.i18n.text
local translate = workpane.i18n.translate

local connectionsView = {}

local validationKeys = {
    ai_api_key_missing = "ai.validation.api-key",
    ai_model_invalid = "ai.validation.model",
    ai_address_invalid = "ai.validation.connection-address",
    ai_connection_duplicate = "ai.validation.connection-duplicate",
    ai_output_budget_unknown = "ai.validation.output-budget-unknown",
    ai_output_budget_whole_window = "ai.validation.output-budget-whole-window",
    ai_extra_parameter_invalid = "ai.validation.extra-parameter-invalid",
}

-- What the reader typed as extra parameters is refused with the rule it breaks, including where its JSON stops making sense.
local function extrasProblem(value)
    if value == nil or value:match("^%s*$") then
        return nil
    end

    local document, refused = codec.read(value, true)

    if document == nil then
        return text("ai.validation.extra-parameters-syntax", refused.detail)
    end

    if type(document) ~= "table" or codec.isList(document) or getmetatable(document) ~= nil then
        return text("ai.validation.extra-parameters-object")
    end

    for key in pairs(document) do
        if key:match("^%s*$") then
            return text("ai.validation.extra-parameters-key")
        end
    end

    return nil
end

local function providerOptions()
    local options = {}

    for index, provider in ipairs(catalog.answering("chat")) do
        options[index] = { value = provider.id, text = text(provider.title) }
    end

    return options
end

-- A value a parameter takes is one of its options or a number within its bounds.
local function fits(parameter, value)
    if parameter.type == "enumeration" then
        for _, option in ipairs(parameter.options) do
            if option.id == value then
                return true
            end
        end

        return false
    end

    return type(value) == "number" and value >= parameter.minimum and value <= parameter.maximum
end

-- A parameter is edited with the control its type asks for, and a value stored for another model starts from the declared default.
local function parameterControl(parameter, value)
    if parameter.type == "enumeration" then
        local options = {}

        for index, option in ipairs(parameter.options) do
            options[index] = { value = option.id, text = text(option.title) }
        end

        return ui.combo({ value = value, options = options, width = 220 })
    end

    local integer = parameter.type == "integer"
    return ui.numberField({ value = value, minimum = parameter.minimum, maximum = parameter.maximum, step = integer and 1 or 0.1, decimals = integer and 0 or 2, width = 160 })
end

function connectionsView.dialog(existing)
    local initialProvider = existing ~= nil and catalog.provider(existing.providerId) or catalog.answering("chat")[1]
    local draft = existing ~= nil and existing or connections.declared(initialProvider, "")
    local discovered = {}
    local controls = {}
    local dialog

    controls.provider = ui.combo({ value = draft.providerId, options = providerOptions(), sorted = true })
    controls.model = ui.textField({ value = draft.modelId, grow = 1, monospace = true })
    controls.models = ui.menuButton({ icon = "more", variant = "icon", items = {} })
    controls.refresh = ui.button({ icon = "refresh", variant = "icon", tooltip = text("ai.settings.refresh-models") })
    controls.display = ui.textField({ value = draft.displayName, placeholder = text("ai.connection.display-name-placeholder") })
    controls.apiKey = ui.secretField({ value = draft.apiKey, placeholder = text("ai.settings.api-key-placeholder"), confirmReveal = true })
    controls.address = ui.textField({ value = draft.address, placeholder = text("ai.connection.address-placeholder") })
    controls.form = ui.column({ spacing = 12 }, {})
    controls.extras = ui.textArea({ value = draft.extraParameters or "", placeholder = text("ai.connection.extra-parameters-placeholder"), rows = 4 })
    controls.problem = ui.alert({ text = "", visible = false })
    local parameterNodes = {}
    local shownFor
    local rows = {
        provider = ui.formField({ label = text("ai.settings.provider") }, controls.provider),
        model = ui.formField({ label = text("ai.settings.model") }, ui.row({ spacing = 6 }, { controls.model, controls.models, controls.refresh })),
        display = ui.formField({ label = text("ai.connection.display-name"), hint = text("ai.connection.display-name-hint") }, controls.display),
        apiKey = ui.formField({ label = text("ai.settings.api-key") }, controls.apiKey),
        address = ui.formField({ label = text("ai.connection.address") }, controls.address),
        extras = ui.column({ spacing = 6 }, { ui.sectionTitle({ text = text("ai.connection.extra-parameters") }), ui.label({ text = text("ai.connection.extra-parameters-description"), style = "muted" }), controls.extras }),
    }

    local function provider()
        return catalog.provider(controls.provider:get("value"))
    end

    local function renderModels()
        local descriptor = provider()
        local items = {}
        local seen = {}

        for _, model in ipairs(descriptor.models) do
            seen[model.id] = true
            items[#items + 1] = { id = model.id, text = model.name }
        end

        for _, id in ipairs(discovered) do
            if not seen[id] then
                items[#items + 1] = { id = id, text = id }
            end
        end

        controls.models:set({ items = items, enabled = #items > 0 })
        controls.refresh:set({ visible = descriptor.protocol ~= "command-line" })
    end

    -- The rows follow the provider: a key only when it requires one, an address only when it is self hosted and the parameters of the chosen model.
    -- They are built again only when the provider or the model changed, and a value the reader set is kept while its parameter still takes it.
    local function renderParameters()
        local descriptor = provider()
        local modelId = (controls.model:get("value") or ""):match("^%s*(.-)%s*$")
        local shown = descriptor.id .. "\n" .. modelId

        if shown == shownFor then
            return
        end

        shownFor = shown
        local rowsList = { rows.provider, rows.model, rows.display, rows.apiKey, rows.address }
        local previous = parameterNodes
        parameterNodes = {}

        for _, parameter in ipairs(catalog.parameters(descriptor, modelId)) do
            local current = previous[parameter.id]
            local typed = current ~= nil and current.parameter.type == parameter.type and current.node:get("value") or nil
            local stored = draft.providerId == descriptor.id and draft.modelId == modelId and draft.parameters[parameter.id] or nil
            local value = typed ~= nil and fits(parameter, typed) and typed or (stored ~= nil and stored or parameter.default)
            local node = parameterControl(parameter, value)
            parameterNodes[parameter.id] = { node = node, parameter = parameter }
            rowsList[#rowsList + 1] = ui.formField({ label = text(parameter.title) }, node)
        end

        rowsList[#rowsList + 1] = rows.extras
        rowsList[#rowsList + 1] = controls.problem
        controls.form:setChildren(rowsList)
        rows.apiKey:set({ visible = descriptor.requiresApiKey })
        rows.address:set({ visible = descriptor.addressConfigurable })
        rows.extras:set({ visible = descriptor.protocol ~= "command-line" })
    end

    local function current()
        local parameters = {}

        for id, entry in pairs(parameterNodes) do
            local value = entry.node:get("value")
            parameters[id] = entry.parameter.type == "integer" and math.tointeger(value) or value
        end

        local descriptor = provider()
        return { providerId = descriptor.id, modelId = controls.model:get("value") or "", displayName = controls.display:get("value") or "", apiKey = descriptor.requiresApiKey and (controls.apiKey:get("value") or "") or "", address = descriptor.addressConfigurable and (controls.address:get("value") or "") or "", parameters = parameters, extraParameters = descriptor.protocol ~= "command-line" and (controls.extras:get("value") or "") or "" }
    end

    -- A new provider starts from what it declares: its key reference, its address and its first preferred model.
    controls.provider:on("change", function()
        local declared = connections.declared(provider(), "")
        discovered = {}
        controls.model:set({ value = declared.modelId })
        controls.apiKey:set({ value = declared.apiKey, revealed = false })
        controls.address:set({ value = declared.address })
        renderModels()
        renderParameters()
    end)

    controls.models:on("select", function(event)
        controls.model:set({ value = event.item })
        renderParameters()
    end)

    controls.model:on("blur", renderParameters)
    controls.model:on("submit", renderParameters)
    reveal.guard(controls.apiKey)

    controls.extras:on("change", function(event)
        local problem = extrasProblem(event.value)
        controls.problem:set({ text = problem or "", visible = problem ~= nil })
    end)

    -- A provider that publishes nothing or cannot be reached is told in the language of the reader, and the reason it gave stays in the log.
    controls.refresh:on("click", function()
        local found, list = pcall(chat.discover, current())

        if not found then
            local code = type(list) == "table" and list.code or ""
            workpane.log.warning("ai.settings", type(list) == "table" and tostring(list.message) or tostring(list), { code = code, detail = type(list) == "table" and tostring(list.detail) or "" })
            controls.problem:set({ text = code == "ai_model_discovery_empty" and text("ai.validation.no-model-published") or code == "ai_secret_environment_missing" and text("ai.error.secret-missing", list.detail) or text("ai.validation.models-unreachable"), visible = true })
            return
        end

        discovered = list
        controls.problem:set({ visible = false })
        renderModels()
    end)

    local function save()
        local candidate = current()
        local refused = extrasProblem(candidate.extraParameters)

        if refused ~= nil then
            controls.problem:set({ text = refused, visible = true })
            return
        end

        local accepted, future = pcall(preferences.replaceConnection, existing ~= nil and connections.key(existing) or "", candidate)
        local failure = future

        if accepted then
            failure = select(2, future:await())
        end

        -- A rule the connection breaks is named in the language of the reader, and any other failure stays in the dialog with its reason in the log.
        local key = failure ~= nil and type(failure) == "table" and validationKeys[failure.code] or nil

        if key ~= nil then
            controls.problem:set({ text = text(key, failure.detail), visible = true })
            return
        end

        if failure ~= nil then
            workpane.log.error("ai.settings", type(failure) == "table" and tostring(failure.message) or tostring(failure), { code = type(failure) == "table" and failure.code or "", detail = type(failure) == "table" and tostring(failure.detail) or "" })
            controls.problem:set({ text = text("ai.error.connection-save"), visible = true })
            return
        end

        dialog:close("save")
    end

    renderModels()
    renderParameters()

    dialog = workpane.dialogs.custom({ title = translate(existing ~= nil and "ai.connection.edit" or "ai.connection.add"), width = 700, content = controls.form, buttons = {
        { id = "cancel", text = translate("ai.dialog.cancel") },
        { id = "save", text = translate("ai.dialog.save"), variant = "primary", closes = false },
    }, onButton = function(button)
        if button == "save" then
            save()
        end
    end })

    workpane.await(dialog)
end

return connectionsView
