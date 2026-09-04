# Code Editor

Each open folder is a workspace tab with a stable identity and a canonical absolute root. A folder is
opened once: asking for one a workspace already holds activates that workspace instead of opening a
second.

## The tree

Every workspace owns a live filesystem tree that observes external changes through Qt filesystem
modelling, without periodic scanning on the thread that draws. A watched path is rearmed when its
notification arrives, and returning to the application looks at every open document again, because a
watched file can change without the platform reporting it at all.

The tree carries the shared filter field above it and narrows to the names that match what was typed,
keeping the folders that lead to a match and reading a folder it has to look inside. That walk carries
a declared depth, because a folder reached through a symbolic link can name the folder that holds it.

A file is also found by typing part of its name, ranked by the characters found in order, where a run
found together outranks the same characters found apart and a match in the name outranks one only a
directory above it carries.

## Documents

Text documents accept UTF-8, UTF-8 with a byte order mark, UTF-16 little endian and UTF-16 big endian
up to sixteen mebibytes. Binary, oversized, unavailable and invalidly encoded files are refused
explicitly, and an encoding the editor cannot write back is named rather than opened as text.

A file is written back in the encoding and the mark it arrived in, so opening and saving never rewrites
bytes nobody asked to change. Saves use atomic replacement and content revisions, so an older
asynchronous completion cannot mark newer edits as clean.

A clean document reloads after an external edit and a dirty one keeps its buffer while reporting a
translated conflict. A reload that carries the same text leaves the buffer untouched.

The encoding in the status bar is also the control that changes it, offering every encoding to read the
bytes again in and every one to write them in. Reading again replaces the buffer, so a document with
unsaved work asks first.

## EditorConfig

Resolution walks from the document directory to the workspace root, stops at the first `root = true`
file and applies the farthest file first. The documented `*`, `**`, `?`, `[seq]`, `[!seq]`, `{a,b}` and
`{num1..num2}` patterns are supported, and the glob answers the cases the EditorConfig core test suite
defines.

It supplies indent style, indent size, tab width, end of line, character set, trailing whitespace
trimming, final newline and maximum line length. A property set to `unset` clears what a file above
declared. The `.editorconfig` files that apply to open documents are watched, so creating, changing or
removing one reindents them without reopening anything.

## Highlighting

Every language and every language server lives in a catalog file the plugin carries as a resource, so a
language is added by one entry of data and never by interface code. A pattern declares the role it
paints rather than a colour, and a code colour scheme resolves those roles.

A colour scheme owns the surface the code is read on, including its background, current line, selection
and line numbers, and is selected independently of the application theme.

A line longer than the highlighting bound keeps its text and loses only its colours, and so does a line
that accumulates more decoration than the colours are worth.

## Language servers

Each workspace starts at most one server per language and speaks JSON-RPC over standard input and
output on a thread of its own, so a payload of any size is never parsed where the interface runs.

What the server declares decides what it is asked for: synchronization, save notifications,
completion, definition and hover leave only for a server that offers them. A capability announced
after initialization through `client/registerCapability` counts exactly as one the initialize result
declared. A superseded completion or hover request is cancelled at the server before the next one is
sent.

A proposal is narrowed by the filter text the server declares rather than by the label it is read as,
and a list the server marks incomplete is asked for again on the next keystroke. A diagnostic keeps
the code and the origin it was reported with, a range tagged unnecessary reads muted, one tagged
deprecated is struck through, and what a diagnostic points at opens from the row it belongs to.

The workspace presents the Problems surface, the outline and workspace symbol search, references,
the calls that reach a name and the calls it makes,
occurrence highlighting, semantic tokens, signature help and the indexing progress the server reports.
Diagnostics belong to the server that published them and to the file they name, so the surface shows
every file the workspace analysed and not only the one being read.

A server that exits unexpectedly is started again at most five times inside three minutes, and reaching
that limit stops the integration and is reported once. Turning the preference off stops every running
server and hides the Problems surface.

## Searching the workspace

Text is searched across the whole workspace from its own surface beside the problems, and that search
needs no language server at all. It reads the workspace away from the interface, skips a file whose
bytes are not text or that is larger than the reading bound, stops at the match bound and says when it
stopped there. A query typed after another discards what the earlier one found.

## Preferences

The editor owns its font family, font size, word wrap, colour scheme and whether language servers run.
It opens at ten points and answers the shared zoom keys with its own size and its own default.

## Related

- [Plugins](plugins.md) — every feature and what it owns
