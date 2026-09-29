-- The conversation of an agent task: bubbles in Markdown, the tools each answer called, the turn still streaming and the composer that sends a message with its attachments.
local attachments = include("attachments")
local codec = include("codec")
local engine = include("engine")
local preferences = include("preferences")
local tools = include("tools")
local status = include("views/status")

local ui = workpane.ui
local text = workpane.i18n.text
local translate = workpane.i18n.translate

local conversationView = {}

local bubbleWidth = 620
local avatarSize = 28
local partIcons = { image = "image", document = "document", audio = "audio" }

local function avatar(role, shown)
    if not shown then
        return ui.spacer({ width = avatarSize, height = avatarSize })
    end

    if role == "user" then
        return ui.avatar({ icon = "person", fill = "accent-strong", ink = "on-accent", size = avatarSize })
    end

    return ui.avatar({ icon = "spark", fill = "raised", ink = "text", size = avatarSize })
end

-- A message calls tools only when it names them, so the catalog of tools is read only for such a message.
local function toolRows(message)
    local calls = codec.read(message.toolCalls, true) or {}
    local rows = {}

    if #calls == 0 then
        return rows
    end

    local schemas = tools.schemas(engine.servers())

    for _, call in ipairs(calls) do
        if type(call) == "table" and type(call.name) == "string" then
            local shown = tools.presentation(schemas, call.name, type(call.arguments) == "table" and call.arguments or {}, translate)
            local lines = { ui.label({ text = shown.title, color = "accent-text", style = "strong" }) }

            if shown.activity ~= "" then
                lines[#lines + 1] = ui.label({ text = shown.activity, style = "caption", color = "text-muted" })
            end

            rows[#rows + 1] = ui.row({ spacing = 8 }, { ui.icon({ name = "tool", color = "accent" }), ui.column({ spacing = 2, grow = 1 }, lines) })
        end
    end

    return rows
end

-- Answers what an attachment is called, which is the name of a document and the kind of anything else.
local function partName(part)
    if part.type == "document" then
        return part.name
    end

    return text(part.type == "image" and "ai.message.image-described" or "ai.message.audio-described")
end

-- The summary that replaced the earlier part of a conversation is a note of the conversation itself, drawn across it rather than as a message of the reader.
local function note(message, fontSize)
    return ui.row({ padding = { 4, 36 } }, { ui.card({ padding = { 7, 11 }, spacing = 4, background = "panel", outline = "border", radius = 8, grow = 1 }, {
        ui.row({ spacing = 6 }, { ui.icon({ name = "history", color = "text-muted" }), ui.label({ text = text("ai.conversation.summary"), style = "caption", color = "text-muted" }) }),
        ui.markdown({ text = message.content, breaks = true, fontSize = fontSize, color = "text-muted" }),
    }) })
end

-- The reader writes on the right in the accent and the agent answers on the left, and a run of messages from one side shows its avatar once.
local function bubble(message, showAvatar, fontSize)
    if message.summarizedUntil > 0 then
        return note(message, fontSize)
    end

    local user = message.role == "user"
    local content = {}

    if message.content ~= "" then
        content[#content + 1] = ui.markdown({ text = message.content, breaks = true, fontSize = fontSize, color = user and "on-accent" or "text" })
    end

    for _, part in ipairs(message.parts) do
        if partIcons[part.type] ~= nil then
            content[#content + 1] = ui.row({ spacing = 6 }, { ui.icon({ name = partIcons[part.type], color = user and "on-accent" or "text-muted" }), ui.label({ text = partName(part), style = "caption", color = user and "on-accent" or "text-muted" }) })
        end
    end

    if not user then
        for _, rowNode in ipairs(toolRows(message)) do
            content[#content + 1] = rowNode
        end
    end

    content[#content + 1] = ui.label({ text = workpane.time.localPresentation(message.createdAt), style = "caption", color = user and "on-accent" or "text-muted", align = "end" })
    local body = ui.card({ padding = { 7, 11 }, spacing = 4, background = user and "accent-strong" or "raised", outline = "none", radius = 12, maxWidth = bubbleWidth }, content)

    if user then
        return ui.row({ spacing = 8, padding = { 0, 4 } }, { ui.spacer({ grow = 1 }), body, avatar("user", showAvatar) })
    end

    return ui.row({ spacing = 8, padding = { 0, 4 } }, { avatar("assistant", showAvatar), body, ui.spacer({ grow = 1 }) })
end

-- The chat is read at the size the reader zooms it to, kept as a setting of the plugin, and a size that could not be kept is told.
local function zoom(step)
    local size = preferences.get("chatFontSize")
    local _, failure = preferences.set("chatFontSize", math.max(8, math.min(36, size + step))):await()

    if failure ~= nil then
        workpane.log.error("ai.settings", tostring(failure.message), { code = failure.code, detail = failure.detail })
        workpane.notify.error(translate("ai.plugin.title"), translate("ai.error.settings-save"))
    end
end

function conversationView.build(taskId)
    local closed = false
    local fontSize = preferences.get("chatFontSize")
    local list = ui.column({ spacing = 3, padding = { 10, 6 } }, {})
    local pending = { text = ui.markdown({ text = "", breaks = true, fontSize = fontSize, visible = false }), phase = ui.label({ text = "", style = "caption", color = "text-muted" }), busy = ui.busyIndicator({ running = true, size = 14 }) }
    local built = {}
    local pendingRows = {}
    local shownRunning = false
    local empty = ui.emptyState({ text = text("ai.conversation.empty"), icon = "chat" })
    local scroll = ui.scroll({ grow = 1, follow = true, onTop = function()
        engine.loadOlder(taskId)
    end }, list)
    local composer = ui.textArea({ placeholder = text("ai.conversation.placeholder"), rows = 2, submitOnEnter = true, grow = 1 })
    local attached = {}
    local chips = ui.row({ spacing = 6, padding = { 6, 8, 0, 8 }, visible = false }, {})

    -- The files waiting to leave with the next message show as chips, and pressing one takes it back.
    local function renderAttached()
        local shown = {}

        for index, entry in ipairs(attached) do
            shown[index] = ui.button({ text = entry.name, icon = partIcons[entry.part.type], variant = "toolbar", tooltip = text("ai.conversation.remove-attachment", entry.name), onClick = function()
                table.remove(attached, index)
                renderAttached()
            end })
        end

        chips:setChildren(shown)
        chips:set({ visible = #attached > 0 })
    end

    local function attach()
        local paths = workpane.await(workpane.dialogs.openFile({ title = translate("ai.conversation.attach-title"), multiple = true, filters = { { name = translate("ai.conversation.attach-filter"), patterns = attachments.patterns() } } }))

        for _, path in ipairs(paths or {}) do
            local part, refusal, name = attachments.read(path)

            if part == nil then
                workpane.notify.error(translate("ai.plugin.title"), translate(refusal, name))
            else
                attached[#attached + 1] = { part = part, name = name }
            end
        end

        renderAttached()
    end

    -- The draft and its attachments stay in the composer until the message was kept, so a message that could not be written is not lost.
    local function send()
        local message = (composer:get("value") or ""):match("^%s*(.-)%s*$")
        local parts = {}

        for index, entry in ipairs(attached) do
            parts[index] = entry.part
        end

        if message == "" and #parts == 0 then
            return
        end

        local sent, written = pcall(engine.sendMessage, taskId, message, parts)

        if not sent then
            workpane.log.error("ai.tasks", type(written) == "table" and tostring(written.message) or tostring(written), { code = type(written) == "table" and written.code or "", detail = type(written) == "table" and written.detail or "" })
            workpane.notify.error(translate("ai.plugin.title"), translate("ai.error.conversation-save"))
            return
        end

        if not written then
            return
        end

        composer:set({ value = "" })
        attached = {}
        renderAttached()
    end

    composer:on("submit", send)

    -- The text still arriving shows only while the model streams it, since once the turn calls tools that text is already in the bubble it was kept in.
    local function renderPending()
        local streamed = engine.streamed(taskId)
        local streaming = engine.phase(taskId) == "streaming" and streamed ~= ""
        pending.text:set({ text = streaming and streamed or "", visible = streaming })
        pending.phase:set({ text = status.phase(taskId) })
        pending.busy:set({ visible = not streaming })
    end

    local function pendingRow(showAvatar)
        local key = tostring(showAvatar)

        if pendingRows[key] == nil then
            pendingRows[key] = ui.row({ spacing = 8, padding = { 0, 4 } }, { avatar("assistant", showAvatar), ui.card({ padding = { 7, 11 }, spacing = 4, background = "raised", radius = 12, maxWidth = bubbleWidth }, { ui.row({ spacing = 6 }, { pending.busy, pending.phase }), pending.text }), ui.spacer({ grow = 1 }) })
        end

        return pendingRows[key]
    end

    -- Each message keeps the bubble built for it, so a new message builds one bubble and the others move along unchanged.
    local function render()
        local nodes = {}
        local kept = {}
        local previous

        for _, message in ipairs(engine.loadConversation(taskId)) do
            if message.role ~= "tool" then
                local key = message.id .. "|" .. tostring(previous ~= message.role)
                kept[key] = built[key] or bubble(message, previous ~= message.role, fontSize)
                nodes[#nodes + 1] = kept[key]
                previous = message.role
            end
        end

        built = kept
        shownRunning = engine.runState(taskId) ~= "idle"

        if shownRunning then
            renderPending()
            nodes[#nodes + 1] = pendingRow(previous ~= "assistant")
        end

        list:setChildren(#nodes == 0 and { empty } or nodes)
    end

    -- A new size of the chat builds every bubble again at that size.
    local function resize()
        local size = preferences.get("chatFontSize")

        if size == fontSize then
            return
        end

        fontSize = size
        built = {}
        pendingRows = {}
        pending.text:set({ fontSize = fontSize })
        render()
    end

    local forgetConversation = engine.listen(function(kind, changed)
        if closed or changed ~= taskId then
            return
        end

        if kind == "streamed" then
            renderPending()
        elseif kind == "conversation" or (kind == "run" and (engine.runState(taskId) ~= "idle") ~= shownRunning) then
            render()
        elseif kind == "run" then
            renderPending()
        end
    end)

    local forgetPreferences = preferences.listen(function(key)
        if not closed and key == "chatFontSize" then
            resize()
        end
    end)

    local function forget()
        closed = true
        forgetConversation()
        forgetPreferences()
    end

    -- A conversation that cannot be read leaves nothing listening behind it.
    local loaded, failure = pcall(render)

    if not loaded then
        forget()
        error(failure, 0)
    end

    local header = ui.row({ padding = { 4, 8 }, spacing = 2 }, {
        ui.spacer({ grow = 1 }),
        ui.button({ icon = "zoom-out", variant = "icon", tooltip = text("ai.conversation.zoom-out"), onClick = function()
            zoom(-1)
        end }),
        ui.button({ icon = "zoom-in", variant = "icon", tooltip = text("ai.conversation.zoom-in"), onClick = function()
            zoom(1)
        end }),
    })

    local node = ui.column({ grow = 1, height = 0 }, {
        header,
        ui.divider({}),
        scroll,
        ui.divider({}),
        chips,
        ui.row({ padding = 8, spacing = 8 }, { ui.button({ icon = "attach", variant = "icon", tooltip = text("ai.conversation.attach"), onClick = attach }), composer, ui.button({ icon = "forward", variant = "primary", tooltip = text("ai.conversation.send"), onClick = send }) }),
    })

    return node, forget
end

return conversationView
