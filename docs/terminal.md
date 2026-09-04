# Terminal

The terminal is a plugin like every other feature. The emulation engine underneath it is a
core-owned shared primitive, so any plugin that embeds a terminal uses the same one.

## Workspaces and layouts

A workspace is a renameable tab holding one layout. A layout is one of the presets, from a single
terminal up to twelve slots, and every terminal keeps a stable identity inside its workspace.

A terminal that does not fit the current layout moves to the shelf rather than being closed, and comes
back when a slot is free. Focus mode gives one terminal the whole area. A slot whose session is gone
shows what an empty slot shows.

Restarting a terminal keeps its identity, its directory, its shell profile and its history file.

## The engine

Emulation goes through the pinned `libghostty-vt` dependency. Unix-like systems use `forkpty` and
Windows uses ConPTY with RAII handle ownership. Screen buffers and scrollback are never persisted.

The last thing a program wrote reaches the reader before the reader is told that program ended,
whatever order the bytes and the end of the stream arrived in.

## Selection, copy and paste

Output is selected by dragging over it and by double clicking the word under the pointer. The
selection belongs to the emulator rather than to the widget, so it reaches the scrollback, follows a
wrapped line and survives everything the program writes under it. A drag held outside the grid moves
the viewport under it.

Copying unwraps the lines it crossed and drops the blanks that padded them to the width of the
terminal.

| Action | macOS | Windows | Linux |
| --- | --- | --- | --- |
| Copy | `⌘C` | `Ctrl+C` | `Ctrl+Shift+C` |
| Paste | `⌘V` | `Ctrl+V` | `Ctrl+Shift+V` |
| Select all | `⌘A` | `Ctrl+Shift+A` | `Ctrl+Shift+A` |
| Clear buffer | `⌘K` | `Ctrl+Shift+K` | `Ctrl+Shift+K` |
| Close terminal | `⌘W` | `Ctrl+Shift+W` | `Ctrl+Shift+W` |

Copy answers only while something is selected, so the combination that interrupts the shell still
reaches it. The shifted forms exist because `Ctrl+C`, `Ctrl+A`, `Ctrl+K` and `Ctrl+W` all mean
something to a shell.

A shell runs every line a plain paste delivers, so text is handed over between the bracketed markers
whenever the program asked to receive it that way, and the confirmation warns about text that would
run rather than about text that merely wraps.

## What a program can ask for

A program that asked for the mouse receives every press, release, drag and wheel notch in the protocol
it selected, on both axes, and the shift modifier claims that gesture back for the selection. A
program that asked for focus reporting is told when the terminal gains and loses it.

The cursor is drawn in the shape the program asked for and blinks only while it asked and the terminal
has focus. A bell rung while nobody is looking is reported in the header of its pane until the reader
comes back.

A program reaches the clipboard only while the reader allows it and only with text.

## Searching

The native find key searches the rows on view and the history behind them. Every match is marked while
the one being read carries the selection, the count says how many there are, and stepping past the
last one comes back to the first. Counting stops at a bound and the count says so.

The find bar floats over the content instead of taking rows from it, so searching never resizes
anything.

## Addresses and drops

An address a program marked on its own cells is opened from them, and one written as plain text is the
word the blanks around it delimit. Only schemes the application can reach are offered, and the platform
modifier with a click opens one. The terminal hands the address to the browser plugin and falls back to
the system browser.

A file dropped on the terminal writes its quoted path to the shell. A drop carrying anything the shell
would act on as more than a path delivers nothing at all.

## Preferences

The terminal owns its font family, font size, ANSI theme, paste confirmation and whether a program may
write to the clipboard. It opens at ten points and answers the shared zoom keys with its own size and
its own default.

Terminal ANSI colour schemes are terminal content themes, selected independently of the application
theme.

## Related

- [Plugins](plugins.md) — every feature and what it owns
