-- Finds what the agent ecosystems leave on disk for an agent: the instructions of the repository and of the reader, their skills and the plugin folders that carry skills, commands, agents and servers.
local codec = include("codec")
local fs = require("fs")
local frontmatter = include("frontmatter")
local spaces = include("spaces")

local resources = {}

local maximumInstructionBytes = 96 * 1024
local maximumDocumentBytes = 256 * 1024
local maximumImportDepth = 4
local maximumAscent = 64
local maximumSkills = 400
local maximumEntries = 500
local maximumPlugins = 64

-- Each folder from the root of the repository to the working directory gives the first of these it holds, so a repository with AGENTS.md is read by it and one with only CLAUDE.md by that.
local instructionNames = { "AGENTS.md", "CLAUDE.md", "AGENT.md", "GEMINI.md", ".github/copilot-instructions.md", ".cursorrules", ".windsurfrules" }
local homeInstructions = { ".agents/AGENTS.md", ".codex/AGENTS.md", ".config/opencode/AGENTS.md", ".config/amp/AGENTS.md", ".claude/CLAUDE.md", ".gemini/GEMINI.md" }
local projectSkillFolders = { ".agents/skills", ".claude/skills", ".codex/skills", ".gemini/skills", ".opencode/skills", ".opencode/skill", ".cursor/skills", ".github/skills", ".kimi/skills", ".windsurf/skills", ".devin/skills", ".goose/skills", ".cline/skills", ".clinerules/skills" }
local homeSkillFolders = { ".agents/skills", ".config/agents/skills", ".claude/skills", ".codex/skills", ".gemini/skills", ".config/opencode/skills", ".config/opencode/skill", ".cursor/skills", ".copilot/skills", ".kimi/skills", ".codeium/windsurf/skills", ".config/devin/skills", ".config/goose/skills", ".cline/skills" }
local projectPluginFolders = { ".agents/plugins", ".gemini/extensions" }
local homePluginFolders = { ".agents/plugins", ".gemini/extensions" }
local homePluginCaches = { ".claude/plugins/cache", ".codex/plugins/cache" }
local pluginManifests = { ".claude-plugin/plugin.json", ".codex-plugin/plugin.json", ".cursor-plugin/plugin.json", ".plugin/plugin.json", "gemini-extension.json", "plugin.json" }

local cache = {}

local trimmed = spaces.trim

local function joined(folder, name)
    return folder:sub(-1) == "/" and folder .. name or folder .. "/" .. name
end

-- The parent of a folder, or the folder itself at the root of a drive, a share or the file system.
local function parent(path)
    local above = path:match("^(.*)/[^/]+$")

    if above == nil or above == "" or above:match("^%a:$") ~= nil or above:match("^//[^/]+$") ~= nil then
        return path:match("^/[^/]+$") ~= nil and "/" or path
    end

    return above
end

local function stat(path)
    local info = fs.stat(path):await()
    return type(info) == "table" and info or nil
end

local function isFile(path)
    local info = stat(path)
    return info ~= nil and info.isFile == true
end

local function canonical(path)
    local resolved, failure = workpane.files.canonical(path):await()
    return failure == nil and resolved or nil
end

local function within(path, scope)
    local prefix = scope:sub(-1) == "/" and scope or scope .. "/"
    return path == scope or path:sub(1, #prefix) == prefix
end

-- A link is listed as itself, so it counts as the folder or the file it reaches.
local function entries(path)
    local listed, failure = workpane.files.list(path):await()
    local found = {}

    for index, entry in ipairs(failure == nil and listed or {}) do
        if index > maximumEntries then
            break
        end

        local reached = entry.kind == "symlink" and canonical(joined(path, entry.name)) or nil
        local target = reached ~= nil and stat(reached) or nil
        local kind = target == nil and entry.kind or target.isDir and "directory" or target.isFile and "file" or entry.kind
        found[#found + 1] = { name = entry.name, kind = kind }
    end

    return found
end

-- A document is read up to its bound, and a longer one answers its beginning and that it was cut.
local function readDocument(path, bound)
    local info = stat(path)

    if info == nil or not info.isFile then
        return nil, false
    end

    local content, failure = fs.readFile(path):await()

    if failure ~= nil or type(content) ~= "string" then
        return nil, false
    end

    if #content > bound then
        return content:sub(1, bound), true
    end

    return content, false
end

-- The repository is the nearest folder above the working directory that holds `.git`, and a working directory outside any repository is its own root.
local function repositoryRoot(workdir)
    local folder = workdir

    for _ = 1, maximumAscent do
        if fs.exists(joined(folder, ".git")) then
            return folder
        end

        local above = parent(folder)

        if above == folder then
            return workdir
        end

        folder = above
    end

    return workdir
end

local function foldersDown(root, workdir)
    local folders = { workdir }
    local folder = workdir

    while folder ~= root do
        local above = parent(folder)

        if above == folder then
            return { workdir }
        end

        table.insert(folders, 1, above)
        folder = above
    end

    return folders
end

-- An import written as `@path` outside code names a document relative to the one importing it, or to the home folder with `~/`, and joins the instructions after it only when it stays inside the folder its first document belongs to.
local function importsOf(content, folder, home)
    local imported = {}
    local fenced = false

    for line in (content .. "\n"):gmatch("(.-)\n") do
        if line:match("^%s*```") ~= nil or line:match("^%s*~~~") ~= nil then
            fenced = not fenced
        end

        local prose = fenced and "" or line:gsub("`[^`]*`", "")

        for path in (" " .. prose):gmatch("%s@([%w%._%-/~]+[%w_%-])") do
            local resolved = path:sub(1, 2) == "~/" and joined(home, path:sub(3)) or path:sub(1, 1) == "/" and path or joined(folder, path)
            imported[#imported + 1] = resolved
        end
    end

    return imported
end

-- A document joins the instructions once, with the documents it imports after it, within the depth and the bytes the instructions keep.
-- A repository reaches only its own files and the reader only the home folder, so a cloned repository never brings a key or a credential of the reader into the prompt.
local function addDocument(instructions, path, label, depth, home, scope)
    local real = canonical(path)

    if real == nil or not within(real, scope) or instructions.seen[real] or depth > maximumImportDepth or instructions.bytes >= maximumInstructionBytes then
        return
    end

    local folder = real:match("^(.*)/[^/]+$") or ""
    local content, cut = readDocument(real, math.min(maximumDocumentBytes, maximumInstructionBytes - instructions.bytes))

    if content == nil or trimmed(content) == "" then
        return
    end

    instructions.seen[real] = true
    instructions.bytes = instructions.bytes + #content
    instructions.documents[#instructions.documents + 1] = { path = path, label = label, content = trimmed(content), cut = cut }

    if cut then
        workpane.log.warning("agent.resources", "An instruction document passed the bound of the instructions and was cut", { detail = path })
    end

    for _, imported in ipairs(importsOf(content, folder, home)) do
        if isFile(imported) then
            addDocument(instructions, imported, imported:match("[^/]+$"), depth + 1, home, scope)
        end
    end
end

-- The instructions of the reader come first and those of the repository follow from its root to the working directory, so the nearest folder speaks last.
local function instructionsFor(workdir, home)
    local instructions = { documents = {}, seen = {}, bytes = 0 }
    local homeScope = canonical(home) or home

    for _, relative in ipairs(homeInstructions) do
        local path = joined(home, relative)

        if isFile(path) then
            addDocument(instructions, path, "~/" .. relative, 1, home, homeScope)
            break
        end
    end

    if workdir == "" then
        return instructions.documents
    end

    local root = repositoryRoot(workdir)
    local repositoryScope = canonical(root) or root

    for _, folder in ipairs(foldersDown(root, workdir)) do
        for _, name in ipairs(instructionNames) do
            local path = joined(folder, name)

            if isFile(path) then
                local label = folder == root and name or folder:sub(#root + 2) .. "/" .. name
                addDocument(instructions, path, label, 1, home, repositoryScope)
                break
            end
        end
    end

    return instructions.documents
end

local function claimed(skills, name)
    for _, skill in ipairs(skills) do
        if skill.name:lower() == name:lower() then
            return true
        end
    end

    return false
end

-- A skill is a folder holding `SKILL.md` whose front matter names it and says when to use it, and a skill without a description is left out with a warning.
local function scanSkills(folder, source, plugin, skills)
    for index, entry in ipairs(entries(folder)) do
        if index > maximumEntries or #skills >= maximumSkills then
            return
        end

        local path = joined(joined(folder, entry.name), "SKILL.md")
        local content = entry.kind == "directory" and readDocument(path, maximumDocumentBytes) or nil
        local fields = content ~= nil and frontmatter.read(content) or {}
        local declared = frontmatter.text(fields, "name")
        local name = (plugin ~= nil and plugin .. ":" or "") .. (declared ~= "" and declared or entry.name)
        local description = frontmatter.text(fields, "description")

        if content ~= nil and description == "" then
            workpane.log.warning("agent.resources", "A skill declares no description and was left out", { detail = path })
        end

        if content ~= nil and description ~= "" and not claimed(skills, name) then
            skills[#skills + 1] = { name = name, description = description, path = path, folder = joined(folder, entry.name), source = source, plugin = plugin, license = frontmatter.text(fields, "license"), compatibility = frontmatter.text(fields, "compatibility"), allowedTools = frontmatter.text(fields, "allowed-tools"), metadata = type(fields.metadata) == "table" and fields.metadata or {} }
        end
    end
end

local function manifestOf(folder)
    for _, relative in ipairs(pluginManifests) do
        local path = joined(folder, relative)
        local content = isFile(path) and readDocument(path, maximumDocumentBytes) or nil
        local decoded = content ~= nil and codec.read(content) or nil

        if type(decoded) == "table" then
            return decoded, relative
        end
    end

    return nil, nil
end

-- A component a manifest names is one path or a list of paths inside the plugin, and the folder of its convention is read when the manifest names none.
local function componentFolders(folder, declared, convention)
    local paths = type(declared) == "string" and { declared } or type(declared) == "table" and declared or { convention }
    local folders = {}

    for _, relative in ipairs(paths) do
        if type(relative) == "string" and not relative:find("..", 1, true) then
            folders[#folders + 1] = joined(folder, (relative:gsub("^%./", "")))
        end
    end

    return folders
end

-- The files of a component are named by their path inside the plugin, which is how the agent reads them.
local function filesOf(plugin, folders, extensions)
    local files = {}

    for _, folder in ipairs(folders) do
        for index, entry in ipairs(entries(folder)) do
            local extension = entry.name:match("%.(%w+)$")

            if index <= maximumEntries and entry.kind == "file" and extension ~= nil and extensions[extension:lower()] then
                files[#files + 1] = joined(folder, entry.name):sub(#plugin + 2)
            end
        end
    end

    return files
end

local function serversOf(folder, manifest)
    local names = {}
    local declared = type(manifest.mcpServers) == "table" and manifest.mcpServers or nil

    for _, file in ipairs({ ".mcp.json", "mcp.json" }) do
        local content = declared == nil and isFile(joined(folder, file)) and readDocument(joined(folder, file), maximumDocumentBytes) or nil
        local decoded = content ~= nil and codec.read(content) or nil
        declared = type(decoded) == "table" and type(decoded.mcpServers) == "table" and decoded.mcpServers or declared
    end

    for name in pairs(declared or {}) do
        names[#names + 1] = tostring(name)
    end

    table.sort(names)

    return names
end

-- A plugin folder of Claude Code, Codex, Gemini CLI or the Agent Plugins standard is read by its manifest, and contributes its skills under its own name, so two plugins never claim one skill.
local function readPlugin(folder, source, plugins, skills)
    if #plugins >= maximumPlugins then
        return
    end

    local manifest, manifestPath = manifestOf(folder)

    if manifest == nil then
        return
    end

    local name = type(manifest.name) == "string" and manifest.name ~= "" and manifest.name or folder:match("[^/]+$")

    for _, known in ipairs(plugins) do
        if known.name == name then
            return
        end
    end

    for _, skillFolder in ipairs(componentFolders(folder, manifest.skills, "skills")) do
        scanSkills(skillFolder, source, name, skills)
    end

    plugins[#plugins + 1] = {
        name = name,
        folder = folder,
        source = source,
        manifest = manifestPath,
        version = type(manifest.version) == "string" and manifest.version or "",
        description = type(manifest.description) == "string" and manifest.description or "",
        commands = filesOf(folder, componentFolders(folder, manifest.commands, "commands"), { md = true, toml = true }),
        agents = filesOf(folder, componentFolders(folder, manifest.agents, "agents"), { md = true, toml = true }),
        servers = serversOf(folder, manifest),
        hooks = isFile(joined(folder, "hooks/hooks.json")) or manifest.hooks ~= nil,
        context = type(manifest.contextFileName) == "string" and manifest.contextFileName or isFile(joined(folder, "GEMINI.md")) and "GEMINI.md" or "",
    }
end

local function scanPlugins(folder, source, depth, plugins, skills)
    for index, entry in ipairs(entries(folder)) do
        local path = joined(folder, entry.name)

        if index > maximumEntries or #plugins >= maximumPlugins then
            return
        end

        if entry.kind == "directory" and depth == 0 then
            readPlugin(path, source, plugins, skills)
        elseif entry.kind == "directory" then
            scanPlugins(path, source, depth - 1, plugins, skills)
        end
    end
end

local function scan(workdir, home)
    local found = { instructions = instructionsFor(workdir, home), skills = {}, plugins = {} }
    local projects = {}

    if workdir ~= "" then
        local root = repositoryRoot(workdir)
        projects = root == workdir and { workdir } or { workdir, root }
    end

    for _, project in ipairs(projects) do
        for _, relative in ipairs(projectSkillFolders) do
            scanSkills(joined(project, relative), relative, nil, found.skills)
        end
    end

    for _, relative in ipairs(homeSkillFolders) do
        scanSkills(joined(home, relative), "~/" .. relative, nil, found.skills)
    end

    for _, project in ipairs(projects) do
        for _, relative in ipairs(projectPluginFolders) do
            scanPlugins(joined(project, relative), relative, 0, found.plugins, found.skills)
        end
    end

    for _, relative in ipairs(homePluginFolders) do
        scanPlugins(joined(home, relative), "~/" .. relative, 0, found.plugins, found.skills)
    end

    -- An installed plugin of Claude Code or Codex lives in the cache of its marketplace, one folder per version.
    for _, relative in ipairs(homePluginCaches) do
        scanPlugins(joined(home, relative), "~/" .. relative, 2, found.plugins, found.skills)
    end

    return found
end

-- What a working directory carries is read once per run and kept for the turns of that run.
function resources.discover(workdir, refresh)
    local key = workdir or ""

    if refresh or cache[key] == nil then
        cache[key] = scan(key, workpane.system.home())
    end

    return cache[key]
end

function resources.skill(found, name)
    for _, skill in ipairs(found.skills) do
        if skill.name:lower() == name:lower() then
            return skill
        end
    end

    return nil
end

function resources.plugin(found, name)
    for _, plugin in ipairs(found.plugins) do
        if plugin.name:lower() == name:lower() then
            return plugin
        end
    end

    return nil
end

return resources
