-- Fits a conversation into the context window of its model, shortening what tools answered long ago before dropping whole turns.
local messages = include("messages")

local window = {}

local safetyMargin = 0.7
local prunedThreshold = 8192
local prunedHead = 4096
local prunedTail = 1024

function window.estimate(list)
    local total = 0

    for _, message in ipairs(list) do
        total = total + messages.tokens(message)
    end

    return total
end

-- The window has to hold the answer and the tool declarations beside the conversation, and a window nobody declares bounds nothing.
function window.limit(contextWindow, reserved)
    if contextWindow <= 0 then
        return nil
    end

    local available = contextWindow - math.max(reserved, 0)

    return available <= 0 and 0 or math.floor(available * safetyMargin)
end

local function prunedText(text)
    local length = utf8.len(text)

    if length == nil or #text <= prunedThreshold or length <= prunedHead + prunedTail then
        return text
    end

    local head = text:sub(1, utf8.offset(text, prunedHead + 1) - 1)
    local tailStart = utf8.offset(text, -prunedTail)
    local omitted = length - prunedHead - prunedTail

    return head .. "\n[... " .. omitted .. " characters pruned ...]\n" .. text:sub(tailStart)
end

-- A result keeps every part it carried, and only its text is shortened.
local function prunedResult(message)
    local copy = { role = message.role, content = {}, toolCallId = message.toolCallId, failed = message.failed }

    for index, entry in ipairs(message.content) do
        copy.content[index] = entry.type == "text" and { type = "text", text = prunedText(entry.text) } or entry
    end

    return copy
end

-- What a tool answered long ago is the cheapest thing to shorten, so its middle goes before any turn is dropped.
function window.prune(list, limit)
    if limit == nil then
        return 0
    end

    local current = window.estimate(list)
    local pruned = 0

    for index, message in ipairs(list) do
        if current <= limit then
            break
        end

        if message.role == "tool" then
            local shortened = prunedResult(message)
            local saved = messages.tokens(message) - messages.tokens(shortened)

            if saved > 0 then
                current = current - saved
                list[index] = shortened
                pruned = pruned + 1
            end
        end
    end

    return pruned
end

-- Old turns are dropped whole, because an assistant turn with tool calls is invalid without the results that answer it, and the instructions and the task are never dropped.
function window.fit(list, limit)
    local sizes = {}
    local total = 0

    for index, message in ipairs(list) do
        sizes[index] = messages.tokens(message)
        total = total + sizes[index]
    end

    if limit == nil or total <= limit then
        return { messages = list, dropped = {}, preservedHead = 0 }
    end

    local preserved = {}
    local start = 1

    while start <= #list and #preserved < 2 and (list[start].role == "system" or list[start].role == "user") do
        preserved[#preserved + 1] = list[start]
        start = start + 1
    end

    local dropped = {}
    local first = start

    while first <= #list and total > limit do
        dropped[#dropped + 1] = list[first]
        total = total - sizes[first]
        first = first + 1

        while first <= #list and list[first].role == "tool" do
            dropped[#dropped + 1] = list[first]
            total = total - sizes[first]
            first = first + 1
        end
    end

    local head = #preserved

    for index = first, #list do
        preserved[#preserved + 1] = list[index]
    end

    return { messages = preserved, dropped = dropped, preservedHead = head }
end

return window
