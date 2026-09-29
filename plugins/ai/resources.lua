-- Finds the skills and the context documents the published agent ecosystems leave on disk, in the working directory first and then in the home folder.
local fs = require("fs")

local resources = {}

local maximumResources = 400
local maximumRootEntries = 500
local maximumResourceBytes = 128 * 1024
local maximumBundles = 32

local cache = {}

local projectSkills = { ".claude/skills", ".agents/skills", ".cursor/skills", ".opencode/skills", ".codex/skills", ".gemini/skills", ".windsurf/skills", ".skills", "skills" }
local projectBundles = { ".claude/plugins", ".agents/plugins" }
local projectContexts = { "AGENTS.md", "AGENT.md", "CLAUDE.md", "GEMINI.md", ".cursorrules", ".windsurfrules", ".github/copilot-instructions.md" }
local homeSkills = { ".claude/skills", ".agents/skills", ".config/agents/skills" }
local homeContexts = { ".claude/CLAUDE.md", ".agents/AGENTS.md", ".codex/AGENTS.md", ".gemini/GEMINI.md" }

-- The published front matter is the opening fence, its keys and the closing fence, and anything else declares nothing.
local function frontMatter(content, key)
    if content:sub(1, 3) ~= "---" then
        return ""
    end

    local closing = content:find("\n---", 4, true)

    if closing == nil then
        return ""
    end

    for line in (content:sub(4, closing - 1) .. "\n"):gmatch("(.-)\n") do
        local name, value = line:match("^%s*([^:]-)%s*:%s*(.-)%s*$")

        if name == key then
            return value
        end
    end

    return ""
end

local function readSmall(path)
    local info = fs.stat(path):await()

    if info == nil or not info.isFile or info.size > maximumResourceBytes then
        return nil
    end

    local content, failure = fs.readFile(path):await()
    return failure == nil and content or nil
end

local function claimed(found, kind, name)
    for _, resource in ipairs(found) do
        if resource.kind == kind and resource.name:lower() == name:lower() then
            return true
        end
    end

    return false
end

local function listSafely(path)
    local entries, failure = workpane.files.list(path):await()
    return failure == nil and entries or {}
end

-- A skill is a directory holding `SKILL.md` or a single Markdown file, and one without a description is skipped with a warning.
local function scanSkills(root, project, found)
    for index, entry in ipairs(listSafely(root.path)) do
        if index > maximumRootEntries or #found >= maximumResources then
            return
        end

        local flat = entry.kind == "file" and entry.name:lower():match("%.md$") ~= nil
        local name = flat and entry.name:sub(1, -4) or entry.name

        if (flat or entry.kind == "directory") and not claimed(found, "skill", name) then
            local path = root.path .. "/" .. entry.name .. (flat and "" or "/" .. root.file)
            local content = readSmall(path)
            local declared = content ~= nil and frontMatter(content, "name") or ""
            local description = content ~= nil and frontMatter(content, "description") or ""

            if content ~= nil and description == "" then
                workpane.log.warning("agent.resources", "A resource declares no description and was skipped", { detail = path })
            elseif content ~= nil and not claimed(found, "skill", declared ~= "" and declared or name) then
                found[#found + 1] = { kind = "skill", name = declared ~= "" and declared or name, description = description, path = path, root = root.path, project = project }
            end
        end
    end
end

local function scan(workdir, home)
    local found = {}
    local skillRoots = {}
    local bundleRoots = {}
    local contexts = {}

    if workdir ~= "" then
        for _, relative in ipairs(projectSkills) do
            skillRoots[#skillRoots + 1] = { path = workdir .. "/" .. relative, file = "SKILL.md", project = true }
        end

        for _, relative in ipairs(projectBundles) do
            bundleRoots[#bundleRoots + 1] = { path = workdir .. "/" .. relative, project = true }
        end

        for _, relative in ipairs(projectContexts) do
            contexts[#contexts + 1] = { path = workdir .. "/" .. relative, project = true }
        end
    end

    for _, relative in ipairs(homeSkills) do
        skillRoots[#skillRoots + 1] = { path = home .. "/" .. relative, file = "SKILL.md", project = false }
    end

    for _, relative in ipairs(projectBundles) do
        bundleRoots[#bundleRoots + 1] = { path = home .. "/" .. relative, project = false }
    end

    for _, relative in ipairs(homeContexts) do
        contexts[#contexts + 1] = { path = home .. "/" .. relative, project = false }
    end

    -- A context document is read whole, because the whole document is what joins the instructions.
    for _, root in ipairs(contexts) do
        local name = root.path:match("[^/]+$")
        local content = not claimed(found, "context", name) and readSmall(root.path) or nil

        if content ~= nil and #found < maximumResources then
            found[#found + 1] = { kind = "context", name = name, description = "", path = root.path, root = root.path, content = content:match("^%s*(.-)%s*$"), project = root.project }
        end
    end

    for _, root in ipairs(skillRoots) do
        scanSkills(root, root.project, found)
    end

    -- A bundle published as a plugin folder contributes the skills it carries, and bundles never nest.
    local bundles = 0

    for _, root in ipairs(bundleRoots) do
        for _, entry in ipairs(listSafely(root.path)) do
            if entry.kind == "directory" and bundles < maximumBundles and fs.exists(root.path .. "/" .. entry.name .. "/plugin.json") then
                bundles = bundles + 1
                scanSkills({ path = root.path .. "/" .. entry.name .. "/skills", file = "SKILL.md" }, root.project, found)
            end
        end
    end

    return found
end

-- The catalog of a working directory is read once per run and kept for the turns of that run.
function resources.discover(workdir, refresh)
    local key = workdir or ""

    if refresh or cache[key] == nil then
        cache[key] = scan(key, workpane.system.home())
    end

    return cache[key]
end

function resources.ofKind(found, kind)
    local selected = {}

    for _, resource in ipairs(found) do
        if resource.kind == kind then
            selected[#selected + 1] = resource
        end
    end

    return selected
end

function resources.skill(found, name)
    for _, resource in ipairs(found) do
        if resource.kind == "skill" and resource.name:lower() == name:lower() then
            return resource
        end
    end

    return nil
end

return resources
