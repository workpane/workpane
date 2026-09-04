# Architecture

Workpane is plugin-first. Every product feature is a plugin discovered at runtime, and the core knows
none of them by name.

## What the core owns

| Area | Responsibility |
| --- | --- |
| `src/app` | Process initialization and core composition |
| `src/plugins` | Plugin interface, discovery, lifecycle, localization and the asynchronous message bus |
| `src/ui` | Application shell, dynamic navigation, dynamic settings and the shared visual primitives |
| `src/terminal` | Shared terminal engine, shell profiles, ANSI catalogs and the platform PTY backends |
| `src/persistence` | Core state, transactions, schema versions and per-plugin database isolation |
| `src/filesystem` | Generic asynchronous file reads, atomic writes, creation, movement and removal |
| `src/agent` | Agent resource discovery, the Model Context Protocol client and the bounded network reply |
| `src/domain` | Core value types and the result contract |
| `plugins` | Every product feature, each one a Qt shared library implementing the plugin interface |

The core owns startup, the shell, plugin discovery and lifecycle, localization, messaging, scoped
SQLite access, asynchronous filesystem operations, alerts and the shared visual primitives. It does
not know which navigation destinations, feature widgets or plugin-owned settings exist.

The `WorkpaneUi` library owns the shared visual primitives and is linked by the core, the terminal
engine and every plugin, so a change to a shared component reaches every consumer from one source.

## How a plugin is found

Discovery scans the platform plugin directory beside the executable without hardcoded filenames. Each
plugin is a Qt shared library implementing `PluginInterface` through Qt plugin metadata, declaring a
stable lowercase identifier and the identifiers it depends on.

The host validates dependencies before initialization, refuses a missing dependency or a cycle,
initializes dependencies first and shuts plugins down in reverse order. Invalid, missing or
untranslated contribution metadata rejects the whole plugin rather than half-loading it.

## How plugins talk

Plugins never include another plugin's headers and never call one another directly. Everything
crosses the host as an asynchronous JSON capability invocation or event.

A plugin asks for **what it wants**, never for **who does it**. There is no way to address a plugin
by its identity and no way to ask whether one is loaded, so replacing an implementation, adding a
second one or removing one is data rather than a change of code anywhere else.

- A capability is named in three lowercase components, such as `workspace.folder.open`, and is
  answered by exactly one provider.
- The core owns the vocabulary of the shared seams so a provider and a consumer cannot spell one
  differently, and provides none of them itself. A capability only one owner answers is named by that
  owner alone.
- Registration refuses a second plugin claiming a name that is already provided.
- An invocation completes exactly once through a callback bound to a context that cancels it when
  destroyed, and carries a core-owned identity and a thirty-second timeout.
- A capability nobody provides is refused by name rather than left to time out.
- Availability is consulted where the capability is used rather than remembered at startup, because a
  provider registers while it initializes and nothing may depend on that order.
- An event names sender, topic and payload, and reaches every other initialized plugin. Events stay a
  broadcast because a fact nobody has to answer is not a request.
- A receiver validates a consumed payload strictly: an unknown field, a missing field, a wrong type,
  a duplicate identity or an invalid reference rejects the message.

Today the shared seams are `workspace.folder.open`, `workspace.folder.serve`, `workspace.page.open`
and `terminal.workspace.snapshot`. The terminal offers the directory its shell stands in without
knowing that the Code Editor and the Web Server are what answer.

## The agent layer

`src/agent` is a core layer any plugin can link. It owns the resource roots declared as data and the
asynchronous catalog that reads them, the Model Context Protocol client with its stdio and streamable
HTTP transports, and the bound that holds a network answer while its bytes arrive.

None of it knows about the AI plugin. The protocol is a published standard, the roots are the ones the
published ecosystems use, and the budget a server has to answer initialization is given to the client
by whoever configures that server rather than read from a catalog. What stays in the AI plugin is what
is really the AI plugin: the providers, the connections, the conversations, the tool registry whose
refusals are sentences of its own catalog, the board and the scheduling.

## What runs where

The GUI thread coordinates widgets, presentation state and short in-memory mutations only. Everything
that can wait on storage, a process, the filesystem, the network or another thread is asynchronous
and returns a future or completes through a queued callback.

SQLite work runs on a dedicated serialized worker. Content that arrives from outside the application
is framed, parsed and decoded away from the thread that draws, because its size is decided by whoever
sent it, and only the finished result returns.

## Persistence

The source of truth is one `workpane.sqlite3` database in the platform local data directory.

Storage has exactly two shapes: a typed table for what is queried, related or paged, and one settings
document per owner for what is only ever read whole and written whole. Each plugin owns the tables
carrying its identifier prefix and never reads or mutates another plugin's tables, which the scoped
SQL host enforces before SQLite ever prepares a statement.

Every value read from a row is reached by the name of its column through the shared strict readers,
and nothing stored can keep the application from opening: a value an owner cannot use becomes the
declared default rather than a failure.

## Themes and language

Three built-in themes — Green, Blue and Red — are applied to the palette, fonts, shared style sheet,
plugin style sheets, navigation icons and every visible view without restarting plugin runtimes. Every
theme value reaches a style sheet as a named token, never as a literal colour or size.

The interface is available in English and Portuguese. The first run resolves the complete system
locale, then its base language, then English, and an explicit choice always wins on later starts.

## Related

- [Plugins](plugins.md) — the eight features
- [Agent resources](agent-resources.md) — what an agent reads from the workspace and the machine
- [Development](development.md) — the gates a change passes
