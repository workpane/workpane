-- The tags an agent prompt may carry, each answered with what the run really has when the instructions are rendered.
local prompt = {}

local tags = {
    "SYSTEM_PROMPT_DATA", "AGENT_NAME", "AGENT_DESCRIPTION", "TASK_TITLE", "TASK_DESCRIPTION", "TASK_PROMPT", "TASK_WORKDIR", "TASK_ISSUE_URL", "DATE_TIME", "DATE_TIME_UTC", "TIME_ZONE", "LOCALE", "LANGUAGE", "OPERATING_SYSTEM", "USER_NAME", "HOME_DIRECTORY", "TOOLS", "MODEL", "MODEL_TRAITS", "VISION", "SEARCH", "SPEECH", "SERVERS", "CONTEXT_WINDOW", "OUTPUT_BUDGET",
}

local declared = {}

for _, tag in ipairs(tags) do
    declared[tag] = true
end

function prompt.tags()
    return tags
end

-- The description of a tag is named after it, written in lowercase words.
function prompt.descriptionKey(tag)
    return "ai.tag." .. tag:lower():gsub("_", "-")
end

-- A tag nobody declares is refused where the prompt is written, so no run ever meets one it cannot answer.
function prompt.unknownTags(text)
    local unknown = {}
    local seen = {}

    for name in text:gmatch("{{([A-Z0-9_]+)}}") do
        if not declared[name] and not seen[name] then
            seen[name] = true
            unknown[#unknown + 1] = name
        end
    end

    return unknown
end

function prompt.render(text, values)
    return (text:gsub("{{([A-Z0-9_]+)}}", function(name)
        if declared[name] then
            return values[name] or ""
        end

        return nil
    end))
end

return prompt
