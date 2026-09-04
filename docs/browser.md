# Browser

The browser embeds Qt WebEngine with a profile of its own, rooted in the application data directory.

## Tabs

Tabs are movable and closable, with address navigation, back, forward, reload, stop, home and
site-requested tabs. Supported explicit schemes are HTTP, HTTPS, file and about, an address without a
scheme receives HTTPS deterministically, and a malformed or unsupported address is rejected.

A start without any stored tab opens one homepage tab, and exactly one tab is active whenever a tab
exists. Closing the last tab leaves no tab at all so the renderer is released, and the view presents
the empty state with the action that opens a new one.

Every tab mutation queues one asynchronous full-session transaction, and the last queued snapshot is
authoritative. A snapshot that answers after a newer one commits nothing.

## Bookmarks

Bookmarks are ordered and may belong to an ordered group. Ungrouped bookmarks appear under one
translated virtual group that is never stored as a group of its own.

They can be dragged between groups, reordered inside a group or dropped into the ungrouped collection.
Every drag result must contain every known group and bookmark exactly once, and one that lost or
duplicated an identifier is refused and rebuilt from what is stored.

Removing a group preserves its bookmarks by moving them to the ungrouped collection in their existing
relative order. The position of a bookmark is numbered by the write rather than carried by the
bookmark, so a collection that gains bookmarks is still numbered from zero without a gap.

The panel is hidden by default and shown from the toolbar toggle. A bookmark opens in the focused
current tab or in a newly activated one, and a double click opens it in the current tab.

## Startup validation

Startup validates every stored URL, identifier, title, timestamp and the active-tab invariant, and
validates canonical bookmark URLs, group references, per-container positions, names and UTC timestamp
order. Everything survives a restart and a configuration export or import.

## Being asked to open something

Another plugin opens a tab only through the asynchronous `browser.open` request. The browser answers by
revealing its own destination, because a caller asking for a page wants to read it rather than be told
it exists somewhere.

The Web Server uses that contract for its action that opens a served address here, and offers a second
action that opens the same address in the system browser.

## Related

- [Plugins](plugins.md) — every feature and what it owns
- [Web Server](web-server.md) — the plugin that asks the browser to open an address
