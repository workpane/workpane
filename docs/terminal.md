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

Pasting writes what the clipboard holds and asks nothing. A shell runs every line a plain paste
delivers, so text is handed over between the bracketed markers whenever the program asked to receive
it that way.

Clearing empties the screen and the history behind it and then asks the shell to print its prompt
again, so the terminal comes back empty with the directory you are standing in, which is what the
`clear` command leaves behind.

## Moving through the line you are writing

| Action | macOS | Windows | Linux |
| --- | --- | --- | --- |
| Word left and right | `⌥←` `⌥→` | `Ctrl+←` `Ctrl+→` | `Ctrl+←` `Ctrl+→` |
| Start and end of line | `⌘←` `⌘→` | `Home` `End` | `Home` `End` |

These write the sequence the shell of the platform really binds rather than the combination they were
typed as, because no shell binds the modified arrow itself. A POSIX shell reads the meta letter that
zsh, bash and fish all bind, and PSReadLine reads the modified arrow it binds on Windows.

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
the system browser when no plugin browses. A reader who prefers the browser of their own system says
so in the terminal settings, and every address then leaves the application.

A file dropped on the terminal writes its quoted path to the shell. A drop carrying anything the shell
would act on as more than a path delivers nothing at all.

## Preferences

The terminal owns its font family, font size, ANSI theme, whether a program may write to the clipboard
and whether an address opens in the browser of the system. It opens at ten points and answers the
shared zoom keys with its own size and its own default.

Both pseudo-terminal backends start the shell from one environment, so `TERM`, `COLORTERM` and
`TERM_PROGRAM` describe this terminal the same way on macOS, Linux and Windows, and everything else the
reader configured for themselves is carried through untouched. macOS opens a login shell because a
window opened by launchd inherits no profile, and every other platform opens an interactive shell
inside a session that already read one.

## The directory the shell is standing in

The header of a pane, and the actions that hand a folder to the Code Editor or the Web Server, follow
the shell as it walks. A POSIX system answers which directory a process is standing in, so that
platform reads it from the process itself.

Windows answers it for nobody, so the PowerShell integration writes a startup file beside the history
of the terminal and wraps the prompt with one that marks the directory. The profiles you own are read
before it runs and your own prompt is called from inside the wrapper, so nothing you configured is
replaced. A startup file you never changed is left exactly as it is.

Terminal ANSI colour schemes are terminal content themes, selected independently of the application
theme.

## Related

- [Features](features.md) — what each feature owns
