-- The preference documents of each owner, read through declared rules so a stored value the owner cannot use reads as its default.
local bridge = require("workpane.bridge")
local lifecycle = require("workpane.lifecycle")
local rules = require("workpane.rules")
local task = require("workpane.task")
local ui = require("workpane.ui")

local preferences = {}

-- The owner, the rules and the values of a store live where the plugin holding the store cannot write them, so no plugin writes the preferences of another owner.
local Store = {}
local storeType = { __metatable = "workpane.preferences", __index = Store }

local stores = setmetatable({}, { __mode = "k" })
local defined = {}

local function validName(name)
    return type(name) == "string" and name:find("^[a-z0-9]+[a-z0-9%-]*$") ~= nil and name:find("%-$") == nil and name:find("--", 1, true) == nil
end

local function stateOf(store)
    local state = stores[store]

    if state == nil then
        bridge.raise("preferences_store_invalid", "A preferences method was called on a value that is not a preferences store", "")
    end

    return state
end

local function ruleOf(state, key)
    local rule = type(key) == "string" and state.schema[key] or nil

    if rule == nil then
        error(bridge.failure({ code = "preferences_key_undeclared", message = "A preference is used that the schema does not declare", detail = tostring(key) }), 3)
    end

    return rule
end

-- A watcher runs in a task of the owner with a copy of the new value, so a failing or slow watcher never holds the change back.
-- The watchers are taken before any of them runs, so one that stops itself or adds another never changes who hears this change.
local function announce(state, key)
    local value = state.values[key]
    local told = {}

    for _, list in ipairs({ state.watchers[key] or {}, state.watchers["*"] or {} }) do
        table.move(list, 1, #list, #told + 1, told)
    end

    for _, watcher in ipairs(told) do
        task.run(state.owner, "preferences", watcher, rules.copy(value), key)
    end
end

-- The watchers of several changed preferences hear them in the order of their names, so a change of many values always reads the same.
local function announceChanges(state, before)
    local keys = {}

    for key in pairs(state.schema) do
        keys[#keys + 1] = key
    end

    table.sort(keys)

    for _, key in ipairs(keys) do
        if not rules.same(before[key], state.values[key]) then
            announce(state, key)
        end
    end
end

-- Every write carries the whole document and a revision, and only the failure of the latest revision puts the committed document back, as the host does.
local function persist(state)
    state.revision = state.revision + 1
    local revision = state.revision
    local document = rules.copy(state.values)
    local written = bridge.request("workpane_preferences_write", { plugin = state.owner, document = state.document, values = document })

    return bridge.future(function()
        local _, failure = written:await()

        if failure == nil then
            state.committed = document
            return nil
        end

        if revision == state.revision then
            local before = state.values
            state.values = rules.copy(state.committed)
            announceChanges(state, before)
        end

        bridge.report(state.owner, "preferences", "A preference document could not be written", { document = state.document, code = failure.code, error = tostring(failure) })
        error(failure, 0)
    end)
end

local function accepted(rule, key, value)
    local result, problem = rules.accept(rule, value, key)

    if problem ~= nil then
        error(bridge.failure({ code = "preferences_value_invalid", message = "A preference was given a value its rule refuses", detail = problem }), 3)
    end

    return result
end

-- Reads the stored document of an owner, keeping the values the rules accept and telling the owner once about a value that reads as its default instead.
local function load(state)
    local stored = bridge.call("workpane_preferences_read", { plugin = state.owner, document = state.document })
    local values = {}

    for key, rule in pairs(state.schema) do
        local value, problem = nil, nil

        if stored[key] ~= nil then
            value, problem = rules.accept(rule, stored[key], key)
        end

        if problem ~= nil then
            bridge.call("workpane_log", { plugin = state.owner, level = "warning", category = "preferences", message = "A stored preference no longer follows its rule and reads as its default", details = { document = state.document, key = problem } })
        end

        if value == nil then
            value = rules.copy(rule.default)
        end

        values[key] = value
    end

    return values
end

-- Declares one preference document of an owner, proving every rule first, so a schema can never disagree with itself.
function preferences.define(owner, schema, options)
    lifecycle.check(owner)
    local chosen = options or {}

    if type(schema) ~= "table" or getmetatable(schema) ~= nil or type(chosen) ~= "table" then
        bridge.raise("preferences_schema_invalid", "The preferences of an owner are declared by a table of rules and a table of options", owner)
    end

    for field in pairs(chosen) do
        if field ~= "document" then
            bridge.raise("preferences_schema_invalid", "A preferences document takes only its name as an option", tostring(field))
        end
    end

    if chosen.document ~= nil and not validName(chosen.document) then
        bridge.raise("preferences_document_invalid", "A preferences document is named with lowercase letters, numbers and single hyphens", tostring(chosen.document))
    end

    local identity = owner .. ":" .. (chosen.document or "")

    if defined[identity] ~= nil then
        bridge.raise("preferences_defined", "A preferences document is defined once", identity)
    end

    local declared = {}

    for key, rule in pairs(schema) do
        if type(key) ~= "string" then
            bridge.raise("preferences_schema_invalid", "A preference is named with a text", tostring(key))
        end

        declared[key] = rules.declare(rule, key, true)
    end

    local store = setmetatable({}, storeType)
    local state = { owner = owner, document = chosen.document, schema = declared, watchers = {}, revision = 0 }
    state.values = load(state)
    state.committed = rules.copy(state.values)
    stores[store] = state
    defined[identity] = state

    return store
end

-- A withdrawn owner leaves its documents, so the owner defines them anew when it starts again, and its watchers never run once it is gone.
function preferences.forget(owner)
    for identity, state in pairs(defined) do
        if state.owner == owner then
            state.watchers = {}
            defined[identity] = nil
        end
    end
end

function Store:get(key)
    local state = stateOf(self)
    ruleOf(state, key)

    return rules.copy(state.values[key])
end

function Store:values()
    return rules.copy(stateOf(self).values)
end

-- Changes one value at once for every reader and answers a future that settles once the document is durable.
function Store:set(key, value)
    local state = stateOf(self)
    lifecycle.check(state.owner)
    local result = accepted(ruleOf(state, key), key, value)

    if rules.same(result, state.values[key]) then
        return bridge.future(function() end)
    end

    state.values[key] = result
    announce(state, key)

    return persist(state)
end

-- Changes several values together, all of them or none, and writes the document once.
function Store:update(changes)
    local state = stateOf(self)
    lifecycle.check(state.owner)

    if type(changes) ~= "table" then
        bridge.raise("preferences_value_invalid", "Several preferences change through a table of values", "")
    end

    local results = {}

    for key, value in pairs(changes) do
        results[key] = accepted(ruleOf(state, key), key, value)
    end

    local before = state.values
    state.values = rules.copy(before)

    for key, value in pairs(results) do
        state.values[key] = value
    end

    announceChanges(state, before)

    return persist(state)
end

-- Puts back the default of one preference, or of every preference when none is named.
function Store:reset(key)
    local state = stateOf(self)
    lifecycle.check(state.owner)
    local changes = {}

    if key ~= nil then
        changes[key] = ruleOf(state, key).default
    else
        for name, rule in pairs(state.schema) do
            changes[name] = rule.default
        end
    end

    return self:update(changes)
end

-- Runs a handler with the new value each time a preference changes, or each time any preference changes when no key is named, and answers the function that stops it.
function Store:watch(key, handler)
    local state = stateOf(self)
    lifecycle.check(state.owner)
    local watched = key

    if type(key) == "function" and handler == nil then
        watched, handler = "*", key
    else
        ruleOf(state, key)
    end

    if type(handler) ~= "function" then
        bridge.raise("preferences_watcher_invalid", "A preference is watched by a function", tostring(key))
    end

    local list = state.watchers[watched] or {}
    list[#list + 1] = handler
    state.watchers[watched] = list

    return function()
        local current = state.watchers[watched] or {}

        for index, candidate in ipairs(current) do
            if candidate == handler then
                table.remove(current, index)
                return
            end
        end
    end
end

-- The options of a text with choices come from the texts the properties give for each choice, in the order the rule declares.
local function choiceOptions(rule, key, labels)
    if type(labels) ~= "table" then
        error(bridge.failure({ code = "preferences_control_invalid", message = "A control of a text with choices names the text of each choice in its labels", detail = key }), 4)
    end

    local options = {}

    for index, choice in ipairs(rule.choices) do
        if labels[choice] == nil then
            error(bridge.failure({ code = "preferences_control_invalid", message = "A control of a text with choices names the text of each choice in its labels", detail = key .. "." .. choice }), 4)
        end

        options[index] = { value = choice, text = labels[choice] }
    end

    return options
end

-- The controls each kind of rule may be edited with, the first being the one chosen when the properties name none, where a free text edited with a combo takes the options its properties give.
local controls = {
    boolean = { "toggle", "checkbox" },
    number = { "numberField", "slider" },
    choices = { "combo", "radioGroup" },
    text = { "textField", "secretField", "textArea", "combo" },
}

-- The kind of control a rule is edited with, and the property of the control that holds the value.
local function controlOf(rule, key, properties, as)
    local family = rule.type == "integer" and "number" or rule.type

    if rule.type == "string" then
        family = rule.choices ~= nil and "choices" or "text"
    end

    local allowed = controls[family]

    if allowed == nil then
        error(bridge.failure({ code = "preferences_control_invalid", message = "Only a boolean, a number or a text preference has a control of its own", detail = key }), 4)
    end

    local kind = as or allowed[1]
    local fits = false

    for _, candidate in ipairs(allowed) do
        fits = fits or candidate == kind
    end

    if not fits then
        error(bridge.failure({ code = "preferences_control_invalid", message = "A preference is edited only with a control that fits its rule", detail = key .. "." .. tostring(as) }), 4)
    end

    if family == "boolean" then
        return kind, "checked"
    end

    if family == "number" then
        properties.minimum = properties.minimum or rule.minimum
        properties.maximum = properties.maximum or rule.maximum

        if rule.type == "integer" then
            properties.decimals = 0
            properties.step = properties.step or 1
        end
    end

    if family == "choices" then
        properties.options = choiceOptions(rule, key, properties.labels)
        properties.labels = nil
    end

    return kind, "value"
end

-- Builds the control that edits one preference with the bounds and choices of its rule, writes what the reader changes and follows every other change of the value.
-- A text field writes when the reader leaves it or presses Enter, and a value its rule refuses puts the stored value back in the control.
function Store:control(key, properties)
    local state = stateOf(self)
    local rule = ruleOf(state, key)
    local chosen = rules.copy(properties or {})
    local as = chosen.as
    chosen.as = nil
    local kind, property = controlOf(rule, key, chosen, as)
    local node

    local function write(value)
        local written = pcall(self.set, self, key, value)

        if not written then
            node:set({ [property] = state.values[key] })
        end
    end

    chosen[property] = state.values[key]

    if kind == "textField" or kind == "secretField" then
        chosen.onSubmit = function(event)
            write(event.value)
        end

        chosen.onBlur = function(event)
            write(event.value)
        end
    else
        chosen.onChange = function(event)
            write(event[property])
        end
    end

    node = ui[kind](chosen)

    -- The control follows its preference while anything holds it, and a control nobody holds any more stops following it at the next change.
    local held = setmetatable({ node = node }, { __mode = "v" })
    local stop

    stop = self:watch(key, function(value)
        local current = held.node

        if current == nil then
            stop()
            return
        end

        current:set({ [property] = value })
    end)

    return node
end

return preferences
