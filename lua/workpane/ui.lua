-- Builds the retained trees the native shell draws, and routes the events of every node back to the handler that declared it.
local bridge = require("workpane.bridge")
local task = require("workpane.task")

local ui = {}

-- The state of a node, and above all the surface and the plugin it belongs to, lives where the plugin holding the node cannot write it.
local Node = {}
local nodeType = { __metatable = "workpane.node", __index = Node }

local states = setmetatable({}, { __mode = "k" })
local nextNode = 0
local nextCommand = 0
local surfaces = {}

-- The handler of an event is declared as a property named after it, so `onRevealRequest` answers the `reveal-request` event.
local function eventName(property)
    return (property:sub(3):gsub("(%u)", function(letter)
        return "-" .. letter:lower()
    end):sub(2))
end

local function isHandler(key, value)
    return type(key) == "string" and key:match("^on%u") ~= nil and type(value) == "function"
end

local function create(kind, props, children)
    if props ~= nil and type(props) ~= "table" then
        bridge.raise("ui_properties_invalid", "The properties of a node are a table", kind)
    end

    nextNode = nextNode + 1
    local node = setmetatable({}, nodeType)
    local state = { id = nextNode, kind = kind, props = {}, handlers = {}, children = {}, waiting = {}, surface = nil }
    states[node] = state

    for key, value in pairs(props or {}) do
        if isHandler(key, value) then
            state.handlers[eventName(key)] = value
        else
            state.props[key] = value
        end
    end

    for _, child in ipairs(children or {}) do
        if states[child] == nil then
            bridge.raise("ui_child_invalid", "Every child of a node is a node", kind)
        end

        state.children[#state.children + 1] = child
    end

    return node
end

-- Answers the state of a node, refusing a value the SDK did not build as a node.
local function stateOf(node)
    local state = states[node]

    if state == nil then
        bridge.raise("ui_node_invalid", "A node method was called on a value that is not a node", "")
    end

    return state
end

local function serialize(node)
    local state = states[node]
    local children = {}

    for index, child in ipairs(state.children) do
        children[index] = serialize(child)
    end

    return { id = state.id, kind = state.kind, props = state.props, children = children }
end

local function attach(node, surface)
    local state = states[node]
    state.surface = surface
    surface.nodes[state.id] = node

    for _, child in ipairs(state.children) do
        attach(child, surface)
    end
end

-- Commands that waited for these nodes reach them in the order the plugin sent them.
local function deliver(nodes)
    local entries = {}

    for _, node in ipairs(nodes) do
        local state = states[node]

        for _, entry in ipairs(state.waiting) do
            entries[#entries + 1] = entry
        end

        state.waiting = {}
    end

    table.sort(entries, function(first, second)
        return first.sequence < second.sequence
    end)

    -- Each command runs in its own protected call, so one that fails never undoes the mount or silences the commands after it.
    for _, entry in ipairs(entries) do
        local sent, failure = pcall(entry.node.command, entry.node, entry.name, entry.arguments)

        if not sent then
            bridge.report(states[entry.node].surface.owner, "interface", "A command sent before its node was mounted failed", { command = entry.name, error = tostring(failure) })
        end
    end
end

function Node:get(key)
    return stateOf(self).props[key]
end

-- An entry is carried by a list or a table of entries, or anywhere among the children of the items of a tree.
local function carries(entries, id)
    for _, entry in ipairs(entries) do
        if entry.id == id or (type(entry.children) == "table" and carries(entry.children, id)) then
            return true
        end
    end

    return false
end

-- Parts the properties given to a node into the ones sent to the host and the handlers that stay on this side of the bridge.
local function split(state, props)
    local changed = {}
    local handlers = {}
    local any = false

    if type(props) ~= "table" then
        bridge.raise("ui_properties_invalid", "The properties of a node are a table", state.kind)
    end

    for key, value in pairs(props) do
        if isHandler(key, value) then
            handlers[eventName(key)] = value
        else
            changed[key] = value
            any = true
        end
    end

    -- New rows or items that no longer carry the selected entry clear the selection in the same patch, whether the plugin or the reader chose it.
    local entries = (state.kind == "table" and changed.rows) or ((state.kind == "list" or state.kind == "tree") and changed.items) or nil

    if entries ~= nil and changed.selected == nil and state.props.selected ~= nil and state.props.selected ~= "" and not carries(entries, state.props.selected) then
        changed.selected = ""
    end

    return changed, handlers, any
end

-- Keeps the properties and the handlers the host accepted.
local function commit(state, changed, handlers)
    for key, value in pairs(changed) do
        state.props[key] = value
    end

    for name, handler in pairs(handlers) do
        state.handlers[name] = handler
    end
end

-- Changes properties of a node and sends only what changed, while handlers stay on this side of the bridge.
-- Nothing changes on this side until the host accepted the patch, so a refused patch leaves the node as it was on both sides.
function Node:set(props)
    local state = stateOf(self)
    local changed, handlers, any = split(state, props)

    if any and state.surface ~= nil then
        bridge.call("workpane_ui_patch", { plugin = state.surface.owner, surface = state.surface.id, node = state.id, props = changed })
    end

    commit(state, changed, handlers)

    return self
end

-- Replaces the children of a container, and a node already inside it is sent by its identity alone wherever it lands, so it keeps its state.
-- Properties given with the children change in the same step, so a strip of tabs changes its tabs and its pages at once.
function Node:setChildren(children, props)
    local own = stateOf(self)
    local changed, handlers = split(own, props or {})
    local surface = own.surface
    local inside = {}
    local reachable = {}

    local function gather(node)
        for _, child in ipairs(states[node].children) do
            inside[child] = true
            gather(child)
        end
    end

    local function carry(node)
        for _, child in ipairs(states[node].children) do
            reachable[child] = true
            carry(child)
        end
    end

    local function describe(node)
        local state = states[node]
        reachable[node] = true

        if inside[node] then
            carry(node)
            return { id = state.id, kept = true }
        end

        local specs = {}

        for index, child in ipairs(state.children) do
            specs[index] = describe(child)
        end

        return { id = state.id, kind = state.kind, props = state.props, children = specs }
    end

    if type(children) ~= "table" then
        bridge.raise("ui_child_invalid", "The children of a node are a list of nodes", own.kind)
    end

    gather(self)

    local specs = {}

    for index, child in ipairs(children) do
        if states[child] == nil then
            bridge.raise("ui_child_invalid", "Every child of a node is a node", own.kind)
        end

        specs[index] = describe(child)
    end

    -- The children change on this side only once the host accepted them, so refused children leave the node and its surface as they were.
    if surface ~= nil then
        bridge.call("workpane_ui_children", { plugin = surface.owner, surface = surface.id, node = own.id, children = specs, props = changed })
    end

    commit(own, changed, handlers)
    own.children = {}

    for index, child in ipairs(children) do
        own.children[index] = child
    end

    if surface == nil then
        return self
    end

    for node in pairs(inside) do
        if not reachable[node] then
            surface.nodes[states[node].id] = nil
            states[node].surface = nil
        end
    end

    local arrived = {}

    for node in pairs(reachable) do
        arrived[#arrived + 1] = node
        states[node].surface = surface
        surface.nodes[states[node].id] = node
    end

    deliver(arrived)

    return self
end

function Node:on(event, handler)
    if type(handler) ~= "function" then
        bridge.raise("ui_handler_invalid", "An event handler is a function", tostring(event))
    end

    stateOf(self).handlers[event] = handler

    return self
end

-- A command sent before the node reaches a surface waits for its mount, so a view can place a cursor or focus a field it has just built.
function Node:command(name, arguments)
    local state = stateOf(self)

    if state.surface == nil then
        nextCommand = nextCommand + 1
        state.waiting[#state.waiting + 1] = { sequence = nextCommand, node = self, name = name, arguments = arguments or {} }
        return self
    end

    bridge.call("workpane_ui_command", { plugin = state.surface.owner, surface = state.surface.id, node = state.id, command = name, arguments = arguments or {} })

    return self
end

function ui.isNode(value)
    return states[value] ~= nil
end

-- Mounts a tree under a surface its owner declares, replacing the tree that surface showed before.
function ui.mount(owner, surfaceId, root)
    if not ui.isNode(root) then
        bridge.raise("ui_root_invalid", "A surface mounts a node", surfaceId)
    end

    -- The previous tree lets go of its surface only once the host accepted the new one, so a refused mount leaves the surface showing what it showed.
    bridge.call("workpane_ui_mount", { plugin = owner, surface = surfaceId, tree = serialize(root) })
    local previous = surfaces[surfaceId]

    if previous ~= nil then
        for _, node in pairs(previous.nodes) do
            states[node].surface = nil
        end
    end

    local surface = { id = surfaceId, owner = owner, nodes = {} }
    attach(root, surface)
    surfaces[surfaceId] = surface

    local arrived = {}

    for _, node in pairs(surface.nodes) do
        arrived[#arrived + 1] = node
    end

    deliver(arrived)
end

function ui.unmount(owner, surfaceId)
    local surface = surfaces[surfaceId]

    if surface == nil then
        return
    end

    for _, node in pairs(surface.nodes) do
        states[node].surface = nil
    end

    surfaces[surfaceId] = nil
    bridge.call("workpane_ui_unmount", { plugin = owner, surface = surfaceId })
end

function ui.unmountOwner(owner)
    for surfaceId, surface in pairs(surfaces) do
        if surface.owner == owner then
            ui.unmount(owner, surfaceId)
        end
    end
end

-- An event names the properties it changed as fields of its value and the new order of the children it moved, which the node takes before its handler runs.
local function remember(state, event)
    for property, field in pairs(event.state or {}) do
        state.props[property] = event.value[field]
    end

    if event.order == nil then
        return
    end

    local byIdentity = {}

    for _, child in ipairs(state.children) do
        byIdentity[states[child].id] = child
    end

    local ordered = {}

    for _, identity in ipairs(event.order) do
        ordered[#ordered + 1] = byIdentity[identity]
    end

    if #ordered == #state.children then
        state.children = ordered
    end
end

-- Every event of a frame arrives in one batch, and each handler runs in its own protected task owned by the plugin of its surface.
bridge.on("workpane.ui.events", function(batch)
    for _, event in ipairs(batch.events) do
        local surface = surfaces[event.surface]
        local node = surface and surface.nodes[event.node]

        if node ~= nil then
            remember(states[node], event)
            local handler = states[node].handlers[event.name]

            if handler ~= nil then
                task.run(surface.owner, "interface", handler, event.value, node)
            end
        end
    end
end)

local containers = { "column", "row", "card", "grid", "stack", "pageHeader", "tabs", "settingsForm", "settingsActions" }
local leaves = { "spacer", "divider", "label", "sectionTitle", "emptyState", "alert", "button", "chip", "menuButton", "textField", "secretField", "textArea", "filterField", "numberField", "slider", "checkbox", "toggle", "radioGroup", "combo", "dateTimeField", "colorField", "layoutSwatch", "badge", "statusIndicator", "busyIndicator", "progress", "icon", "image", "avatar", "list", "tree", "table", "canvas", "codeEditor", "webView", "terminal", "markdown" }

for _, kind in ipairs(containers) do
    ui[kind] = function(props, children)
        return create(kind, props, children)
    end
end

for _, kind in ipairs(leaves) do
    ui[kind] = function(props)
        return create(kind, props, nil)
    end
end

function ui.scroll(props, child)
    return create("scroll", props, { child })
end

function ui.popover(props, child)
    return create("popover", props, { child })
end

function ui.splitter(props, first, second)
    return create("splitter", props, { first, second })
end

function ui.settingsRow(props, control)
    return create("settingsRow", props, { control })
end

function ui.formField(props, control)
    return create("formField", props, { control })
end

return ui
