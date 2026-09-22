# Agent resources

An agent reads what the published ecosystems left on disk, in the workspace it is running in and in
the roots of the machine. It **installs nothing and writes nothing** into them: a workspace prepared
for any agent works here unchanged, and leaves unchanged.

This layer is `src/agent` in the core, so it belongs to no plugin and any plugin can link it.

## What is read

| Kind | Layout | What it contributes |
| --- | --- | --- |
| Skill | A directory holding `SKILL.md`, or a single Markdown file | Name and description, disclosed progressively |
| Command | A Markdown file with front matter | Name and description |
| Agent | A Markdown file with front matter | Name and description of a subagent definition |
| Context | `AGENTS.md`, `AGENT.md`, `CLAUDE.md` | The whole document, joined into the prompt |
| Bundle | A directory carrying its own manifest | The skills, commands and agents it ships |
| Server catalog | `.mcp.json` and the settings files other tools write | The servers a project already declares |

A skill, a command and an agent declare themselves in front matter carrying `name` and `description`.
A context file and a server catalog have no front matter, so the whole document is the resource.

## Where it is read from

Roots are declared as data, so supporting one more ecosystem is one more entry and never a change of
code. Project roots come first, because the workspace the agent runs in overrides what the machine
offers, and the first root that declares a name owns it.

Project roots cover `.claude`, `.agents`, `.cursor`, `.opencode`, `.codex`, `.gemini`, `.windsurf`,
`.github` and the bare `.skills` and `skills` directories. Machine roots cover `~/.claude`,
`~/.agents`, `~/.codex`, `~/.cursor`, `~/.gemini` and `~/.config/agents`.

The always-on context documents read in a project are `AGENTS.md`, `AGENT.md`, `CLAUDE.md`,
`GEMINI.md`, `.cursorrules`, `.windsurfrules` and `.github/copilot-instructions.md`, and on the machine
`~/.claude/CLAUDE.md`, `~/.agents/AGENTS.md`, `~/.codex/AGENTS.md` and `~/.gemini/GEMINI.md`. A name
the project declares owns it, so a machine document only applies where the project has none.

The server catalogs read are `.mcp.json`, `.cursor/mcp.json`, `.vscode/mcp.json`,
`.claude/settings.json` and `.claude/settings.local.json` in a project, and `~/.claude.json`,
`~/.claude/settings.json`, `~/.cursor/mcp.json`, `~/.codex/config.toml` and `~/.gemini/settings.json`
on the machine.

A bundle contributes the `skills`, `commands` and `agents` directories it ships, which is how a
published plugin is read without being installed. A bundle carries no bundle of its own, because the
depth of what a workspace declares must not be decided by that workspace.

A document root names one file and is read directly rather than looked for in a listing, because the
published ecosystems name it with a leading dot and a directory listing carries no hidden entry.

## What it refuses to do

A server catalog another tool left in the workspace is read and reported, never connected. Starting a
server declared by a file the reader did not write would run a command nobody chose.

An entry that cannot be used is skipped by name in the log and never drops the catalog around it. A
skill whose front matter declares no description is skipped exactly that way.

Every read goes through the core asynchronous filesystem service, bounded in the number of entries and
in the bytes of each one, and never on the thread that draws.

What was read is kept for the turns of one run, because the prompt and the tools ask for it on every
turn. A run reads the workspace again before its first turn, so a skill or an instruction written
while the application is open reaches the next run rather than waiting for a restart. Runs starting at
the same moment share one walk instead of reading the same forty roots once each.

## Related

- [AI](ai.md) — how an agent uses what is read
- [Architecture](architecture.md) — the agent layer and the capability seams
