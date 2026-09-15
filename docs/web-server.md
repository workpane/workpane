# Web Server

A web server is created from any selected folder and is independent from every other plugin.

## Serving

Directory requests resolve `index.html` before `index.htm`, assets are served with their detected MIME
type and any path outside the configured document root is rejected. Canonical validation prevents
traversal and symbolic-link escape before a file is ever opened.

Request headers, files, connection counts and deadlines are bounded. A connection never buffers more
than one request may occupy, because the size of what a client sends is decided by that client and the
bound has to hold before the bytes are read. A file is written to the socket in bounded chunks and
continued on every write that completes, so a response larger than that bound is the ordinary case.

A file that shrinks while it is being written ends the connection instead of waiting for bytes that are
no longer there, because the same folder is often open in the editor and served at the same time.

Starting validates the identity, the canonical readable root, the numeric bind address and the port.
Stopping aborts active sockets before the worker is destroyed.

## The view

Every configuration carries its own stable identity, display name and document root, and the list shows
status, address and document root with the actions packed together in each row. A running server keeps
its configuration fields read-only until it is stopped.

Request logging is bounded, thread-safe and runtime-only. Timestamps are captured in UTC and rendered
in the system locale and timezone, and request cursors stay monotonic after the log is cleared.

Two actions open a running address: one leaves the application for the system browser and says so with
the glyph of a link opening elsewhere, and one opens it in the [Browser](browser.md) plugin. Either one
says so when the address could not be opened.

## Creating one from somewhere else

A folder arriving from another plugin opens the same form that configures every other server,
pre-filled with that folder, its name and the next port no configuration took. One folder is served by
one configuration, so asking again for a folder already configured opens the form of that server
instead of creating a second.

Nothing is configured until the form is confirmed, and the form closes itself once the server it
configured is running.

## Terminal integration

A configuration may optionally link to one terminal session as an integration source, and terminal data
arrives only through validated JSON messages. Closing that terminal removes only the link and preserves
the configuration and its running instance. Startup reconciliation removes unavailable links without
removing the servers they belonged to.

## Related

- [Plugins](plugins.md) — every feature and what it owns
- [Terminal](terminal.md) — the optional integration source
