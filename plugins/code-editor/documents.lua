-- One file open in the editor: its text, how it was read and how it is written back, whether it changed since, and the editor component that shows it.
local async = require("async")
local catalog = include("catalog")
local editorconfig = include("editorconfig")
local encoding = include("encoding")
local preferences = include("preferences")

local ui = workpane.ui
local translate = workpane.i18n.translate

local documents = {}

local Document = {}
Document.__index = Document

-- Handlers hear a changed state, a moved cursor, a request for completion or hover text, a save and a document the reader should no longer see.
function documents.new(workspace, path, cursor, handlers)
    return setmetatable({ workspace = workspace, path = path, cursor = cursor or { line = 1, column = 1 }, language = catalog.detect(path), text = "", savedText = "", charset = "utf-8", lineEnding = "lf", dirty = false, loaded = false, saving = false, saveQueued = false, saves = 0, closed = false, settling = {}, properties = { indentStyle = "space", indentWidth = 4, unsupportedCharsets = {} }, handlers = handlers }, Document)
end

function Document:name()
    return self.path:match("[^/]+$") or self.path
end

-- Answers the text of one line counted from one, or nil past the last line, found without splitting the whole text.
function Document:line(number)
    local start = 1

    for _ = 2, number do
        local newline = self.text:find("\n", start, true)

        if newline == nil then
            return nil
        end

        start = newline + 1
    end

    return self.text:match("^[^\n]*", start)
end

-- Answers the character just before the cursor, or nil at the start of a line.
function Document:characterBefore()
    local line = self:line(self.cursor.line)

    if line == nil then
        return nil
    end

    local first = utf8.offset(line, self.cursor.column - 1)
    local last = utf8.offset(line, self.cursor.column)

    if self.cursor.column <= 1 or first == nil or last == nil then
        return nil
    end

    return line:sub(first, last - 1)
end

function Document:effectiveCharset()
    return self.manualCharset or self.properties.charset or self.charset
end

function Document:effectiveEnding()
    return self.properties.endOfLine or self.lineEnding
end

local function report(key, detail)
    workpane.notify.error(translate("code-editor.error.title"), translate(key) .. (detail ~= nil and detail ~= "" and "\n" .. detail or ""))
end

function Document:component()
    if self.node ~= nil then
        return self.node
    end

    local scheme = catalog.scheme(preferences.get("colorScheme"))
    self.node = ui.codeEditor({
        value = self.text,
        language = catalog.definition(self.language),
        scheme = scheme.colors,
        fontSize = preferences.get("fontSize"),
        fontFamily = preferences.get("fontFamily"),
        wordWrap = preferences.get("wordWrap"),
        tabSize = self.properties.indentWidth,
        insertSpaces = self.properties.indentStyle == "space",
        completion = false,
        hovers = false,
        grow = 1,
        height = 0,
        onChange = function(event)
            self.text = event.value
            self:refreshDirty()
        end,
        onFlushed = function()
            self:release()
        end,
        onCursor = function(event)
            self.cursor = { line = event.line, column = event.column }
            self.handlers.cursor(self)
        end,
        onCompleteRequest = function(event)
            self.handlers.completion(self, event)
        end,
        onHover = function(event)
            self.handlers.hover(self, event)
        end,
        onZoom = function(event)
            preferences.set("fontSize", math.tointeger(math.floor(event.fontSize + 0.5)))
        end,
    })

    return self.node
end

-- The editor reports its text a moment after the reader stops typing, so saving, closing and reading the file again first ask it for the text on screen.
function Document:settle()
    if self.node == nil or self.closed then
        return
    end

    local settled, resolve = async.deferred()
    self.settling[#self.settling + 1] = resolve
    self.node:command("flush")
    settled:await()
end

-- A closed document answers no more waits, polls or reads, since its editor is gone and will never report its text.
function Document:close()
    self.closed = true
    self:release()
end

-- Everything waiting for the text on screen continues, which also happens when the document closes before its editor answered.
function Document:release()
    local waiting = self.settling
    self.settling = {}

    for _, resolve in ipairs(waiting) do
        resolve()
    end
end

function Document:refreshDirty()
    local dirty = self.text ~= self.savedText

    if dirty ~= self.dirty then
        self.dirty = dirty
        self.handlers.changed(self)
    end

    self.handlers.edited(self)
end

-- The EditorConfig files decide the indentation, and a charset they name that the editor cannot write is told every time they are read.
function Document:configure()
    self.properties = editorconfig.resolve(self.path, self.workspace.root, self.workspace.files)

    if #self.properties.unsupportedCharsets > 0 then
        report("code-editor.error.editorconfig-charset", table.concat(self.properties.unsupportedCharsets, ", "))
    end

    if self.node ~= nil then
        self.node:set({ tabSize = self.properties.indentWidth, insertSpaces = self.properties.indentStyle == "space" })
    end

    self.handlers.changed(self)
end

-- A file is read whole up to the bound, and a read that finds the same bytes again changes nothing.
-- A document the reader changed keeps its text when the file changes outside the product, and the reader is told.
-- A read that crossed a save or a close is dropped, since the bytes it found are older than what the document holds now.
function Document:read(reread)
    local saves = self.saves
    local info, failure = self.workspace.files.stat(self.path):await()

    if failure ~= nil then
        return false, "code-editor.error.read-failed"
    end

    if info == nil or not info.isFile then
        return false, "code-editor.error.file-missing"
    end

    if info.size > (self.workspace.files.remote and self.workspace.files.maximumFileBytes or catalog.limit("maximumFileBytes")) then
        return false, "code-editor.error.file-too-large"
    end

    local read, failure = self.workspace.files.read(self.path):await()

    if failure ~= nil then
        return false, failure.code == "files_read_too_large" and "code-editor.error.file-too-large" or "code-editor.error.read-failed"
    end

    if self.closed or self.saving or self.saves ~= saves then
        return true
    end

    -- A charset chosen by hand comes first and the one the EditorConfig files name next, the same order a save writes with.
    local bytes = read.bytes
    local decoded, refused = encoding.decode(bytes, self.manualCharset or self.properties.charset, preferences.get("defaultCharset"))

    if decoded == nil then
        return false, refused.code, refused.detail
    end

    self.stamp = { size = info.size, mtime = info.mtime }

    if not reread and decoded.digest == self.digest then
        return true
    end

    if not reread and self.dirty then
        report("code-editor.error.external-conflict", self.path)
        self.digest = decoded.digest
        return true
    end

    self.revision = read.revision
    local first = not self.loaded
    self.text = decoded.text
    self.savedText = decoded.text
    self.charset = decoded.charset
    self.lineEnding = decoded.lineEnding
    self.digest = decoded.digest
    self.dirty = false
    self.loaded = true
    self.node:set({ value = self.text })

    if first then
        self.node:command("reveal", { line = self.cursor.line, column = self.cursor.column })
    end

    self.handlers.loaded(self)
    self.handlers.changed(self)

    return true
end

function Document:load()
    self:configure()
    local loaded, key, detail = self:read(true)

    if not loaded then
        report(key, detail ~= nil and detail ~= "" and detail or self.path)
    end

    return loaded
end

-- The bytes are written beside the file and moved over it, so a failed write never leaves half a file, and a save asked for during another one runs after it.
function Document:save()
    if self.saving then
        self.saveQueued = true
        return
    end

    self.saving = true
    self.saves = self.saves + 1
    self.handlers.changed(self)
    local saving <close> = setmetatable({}, { __close = function()
        if self.saving then
            self.saving = false
            self.handlers.changed(self)
        end
    end })

    local text = self.text
    local charset = self:effectiveCharset()
    local bytes = encoding.encode(text, charset, self:effectiveEnding(), { trimTrailingWhitespace = self.properties.trimTrailingWhitespace, insertFinalNewline = self.properties.insertFinalNewline })

    if bytes == nil then
        workpane.notify.error(translate("code-editor.error.title"), translate("code-editor.error.charset-unrepresentable", charset))
        return false
    end

    local written, failure = self.workspace.files.save(self.path, bytes, self.revision):await()

    if written == nil or failure ~= nil then
        workpane.log.error("save", "A document could not be saved", failure ~= nil and { path = self.path, code = failure.code, detail = failure.detail } or { path = self.path })
        report(failure ~= nil and failure.code == "remote_file_conflict" and "code-editor.error.remote-conflict" or "code-editor.error.write-failed", self.path)
    else
        self.revision = written.revision
        self.digest = encoding.digest(bytes)
        self.savedText = text
        self.stamp = nil
        self.removedReported = false
        self:refreshDirty()
        self.handlers.saved(self)
    end

    self.saving = false
    self.handlers.changed(self)

    if self.saveQueued then
        self.saveQueued = false

        if not self.closed and self.workspace.files.available then
            self:save()
        end
    end

    return written ~= nil and failure == nil
end

-- Choosing an encoding by hand reads the file again in it after the reader agrees to lose unsaved changes, and a failed read keeps the previous choice.
function Document:reopenWith(charset)
    self:settle()

    if self.dirty then
        local confirmed = workpane.await(workpane.dialogs.confirm({ title = translate("code-editor.plugin.title"), message = translate("code-editor.status.reopen-title"), detail = translate("code-editor.status.reopen-message"), confirmText = translate("code-editor.status.reopen-action") }))

        if not confirmed then
            return
        end
    end

    local previous = self.manualCharset
    self.manualCharset = charset
    local loaded, key, detail = self:read(true)

    if not loaded then
        self.manualCharset = previous
        report(key, detail)
    end
end

-- A charset that cannot write the text keeps the previous choice, so later saves are not refused for it.
function Document:saveWith(charset)
    self:settle()

    if self.closed then
        return
    end

    local previous = self.manualCharset
    self.manualCharset = charset

    if self:save() == false then
        self.manualCharset = previous
    end

    self.handlers.changed(self)
end

-- A file that disappeared closes a clean document at once, and a changed document stays open with its text while the reader is told.
-- The file is looked at first and the editor is asked for its text only when the file changed, and a save that began meanwhile ends the poll.
function Document:poll()
    if self.saving or not self.loaded or self.closed or not self.workspace.files.available then
        return
    end

    local info, failure = self.workspace.files.stat(self.path):await()

    if failure ~= nil or self.closed or not self.workspace.files.available then
        return
    end

    local gone = info == nil
    local changed = not gone and (self.stamp == nil or info.size ~= self.stamp.size or info.mtime ~= self.stamp.mtime or self.workspace.files.recent(info))

    if not gone and not changed then
        self.removedReported = false
        return
    end

    self:settle()

    if self.saving or self.closed then
        return
    end

    if gone then
        if self.dirty then
            if not self.removedReported then
                self.removedReported = true
                report("code-editor.error.external-removed", self.path)
            end

            return
        end

        self.handlers.removed(self)
        return
    end

    self.removedReported = false
    self:read(false)
end

function Document:moveTo(path)
    self.path = path
    self.language = catalog.detect(path)

    if self.node ~= nil then
        self.node:set({ language = catalog.definition(self.language) })
    end
end

function Document:applyPreferences(key, value)
    if self.node == nil then
        return
    end

    if key == "fontSize" or key == "fontFamily" or key == "wordWrap" then
        self.node:set({ [key] = value })
    elseif key == "colorScheme" then
        self.node:set({ scheme = catalog.scheme(value).colors })
    end
end

return documents
