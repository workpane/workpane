# Plugins

Every product feature is a plugin. The core owns none of them and learns what exists only by
discovering the libraries beside the executable.

| Plugin | Identifier | Depends on | What it owns |
| --- | --- | --- | --- |
| [Terminal](terminal.md) | `terminal` | `logs` | Multipanel terminal workspaces, layouts, shelf, focus mode and terminal preferences |
| [Code Editor](code-editor.md) | `code-editor` | `logs` | Folder workspaces, syntax highlighting, EditorConfig and language-server integration |
| [Browser](browser.md) | `browser` | — | Embedded tabbed browsing with ordered bookmark groups and session restoration |
| [AI](ai.md) | `ai` | `logs` | Agent task workspaces, conversations, provider APIs, tools, executions and scheduling |
| [Web Server](web-server.md) | `web-server` | — | Static web servers with configurable document root, lifecycle and request activity |
| Logs | `logs` | — | Centralized runtime logs with filtering, paging and explicit clearing |
| System Information | `system-information` | — | Operating-system, hardware and display discovery published as immutable snapshots |
| Donate | `donate` | — | Verified donation destinations and supporter presentation |

## What a plugin contributes

Each plugin returns an ordered list of navigation items together with its title key, translation
catalog, style sheet, settings groups and widget factories. A navigation item declares a
plugin-local identifier, a translation key, a native icon, primary or secondary placement and the
position it takes in the bar.

The left mode bar is built entirely from those contributions, and the whole content area to the right
belongs to the selected plugin widget, which the core treats as an opaque widget.

Two destinations claiming one position reject the plugin that declares the second one, so the bar
never falls back to the order the libraries happened to load in. The core Settings destination is
appended after every plugin item and is always last.

## Logs

Every centralized entry carries a UTC timestamp, the source plugin identifier, a level of debug, info,
warning or error, a category, a message and a JSON details object. The viewer loads the newest hundred
entries first and asks for older pages explicitly. Filtering and search work on the loaded pages
without mutating what is stored, and clearing happens only after an explicit action.

## System Information

Collection runs only on a worker thread and publishes one complete immutable snapshot. A refresh
already in progress refuses a duplicate rather than starting overlapping probes, and a failed or
cancelled collection never replaces the last complete snapshot with partial state.

Hardware discovery uses a pinned `hwinfo` revision for the operating system, processors, memory,
graphics, mainboard, disks, batteries and network interfaces. Every screen the window system offers is
reported beside the graphics hardware with its name, resolution, scale, pixel density and refresh
rate, read on the thread that owns the window system because that is the only one allowed to ask.

A field the platform does not supply stays explicitly unavailable and never receives an inferred
value.

## Donate

One secondary navigation item with final priority, presenting the maintainer profile and the verified
donation destinations. The bundled profile image is a plugin-owned resource and never needs a network
request. A destination opens only as an explicit HTTPS address in the system browser, and a dispatch
that fails is reported.

## Related

- [Architecture](architecture.md) — how discovery, messaging and persistence work
