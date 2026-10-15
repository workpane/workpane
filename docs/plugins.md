# Writing a plugin

Everything Workpane does is a Lua plugin, and the C++ core draws, stores, plays and runs whatever a plugin asks of it. A plugin contributes side bar destinations, bands across the top or the bottom of the window and settings sections, builds its views from the components the product draws, draws anything else on a canvas, plays sounds, keeps its preferences and its own tables in the shared database and talks to other plugins through events and capabilities. The reader turns every plugin on and off while the product runs. This page is the reference for all of it, and the bundled Flappy Bird and Task Hero plugins are complete examples of a game in a destination and of an animation that runs by itself in a band.

## Anatomy

A plugin is a folder named after its identifier, inside the `plugins` folder the product carries or inside one of the plugin folders the reader adds in the Plugins group of the settings. Every plugin follows the same layout, so a reader of one plugin knows where to look in any other:

```text
plugins/notes/
├── plugin.lua          The definition: identifier, destinations, bands, settings sections, start, stop and capabilities
├── translations.lua    Every text in every language the product offers
├── view.lua            Builds the destination view the definition names
├── views/editor.lua    A part of the view layer, such as a dialog or a panel, loaded with include("views/editor")
├── settings.lua        Builds the settings sections the definition names
├── preferences.lua     Declares the preferences of the plugin with their rules
├── store.lua           Migrates and reads the tables of the plugin
├── notes.lua           A domain module named for what it is, such as a parser or a client
└── assets/icon.png     Images, sounds and data the plugin reads
```

The definition only declares the plugin and wires its start, its stop and the capabilities it provides, so no view, form or query is built there. A plugin writes only the files it needs, and a plugin without a destination has no `view.lua`. The identifier is lowercase letters, numbers and single hyphens between them, and the folder name must be exactly that identifier. Plugins are discovered in the folder the product carries first and then in the added folders in their order, and a plugin whose identifier an earlier folder already answered is refused.

## The definition

The file `plugin.lua` returns a table describing the plugin:

```lua
local ui = workpane.ui
local text = workpane.i18n.text

local function view()
    return ui.column({ padding = 24, spacing = 12 }, {
        ui.pageHeader({ title = text("notes.view.title") }),
        ui.label({ text = text("notes.view.empty"), style = "muted" }),
    })
end

return {
    id = "notes",
    sdk = 6,
    titleKey = "notes.plugin.title",
    navigation = {
        { id = "main", titleKey = "notes.navigation.main", icon = "edit", placement = "primary", order = 200, view = view },
    },
}
```

| Field | Required | Meaning |
| --- | --- | --- |
| `id` | Yes | The identifier, equal to the folder name |
| `sdk` | Yes | The level of the SDK the plugin is written for, which `workpane.app.sdk` answers, and a plugin declaring another level is refused with `plugin_sdk_unsupported` |
| `titleKey` | Yes | The translation key naming the plugin |
| `descriptionKey` | No | The translation key of the sentence the Plugins group of the settings shows under the title |
| `dependencies` | No | Identifiers of plugins that must run before this one |
| `enabled` | No | A function answering whether the plugin can run at all on this machine, such as a check that a program it needs is installed |
| `offByDefault` | No | True for a plugin that stays off until the reader turns it on, as the games and the components gallery do |
| `navigation` | No | The side bar destinations |
| `bands` | No | The bands across the top or the bottom of the window |
| `settings` | No | The settings groups and their sections |
| `migrations` | No | The lists of statements that create and change the tables of the plugin, which the product applies before every start |
| `start` | No | Runs each time the plugin starts, after it is registered and its tables follow its migrations |
| `stop` | No | Runs each time the plugin stops, when the reader turns it off or the product closes, and may await futures for up to three seconds when the reader turns it off, or for its share of two and a half seconds when the product closes, where the plugins reading the log stop after every other plugin so the log keeps what the other stops wrote |

The identifier is lowercase letters and numbers in words joined by single hyphens and is never `workpane`, and it is checked before any code of the plugin runs, so a folder named otherwise is refused with `plugin_identifier_invalid`, and a `plugin.lua` that answers something other than a table with that identifier is refused with `plugin_identifier_mismatch`. A definition whose `dependencies` is not a list of strings, whose `migrations` is not a list of lists of strings, whose `enabled`, `start` or `stop` is not a function or whose `offByDefault` is not a boolean refuses only its own plugin with `plugin_definition_invalid`, a dependency on a plugin that is not installed with `plugin_dependency_missing`, and a title or a description its own catalog does not spell refuses it with `plugin_title_untranslated`.

A navigation item carries `id`, `titleKey`, an `icon` the product knows, or it is refused with `navigation_icon_unknown`, `placement` of `primary` or `secondary`, an `order` from 0 to 100000, `view` and the optional `shortcuts` and `preload`. Items are ordered by `order` inside their placement, and two plugins claiming the same order in one placement is refused. A view is built the first time its destination is shown, and one with `preload` set is built as soon as every plugin started, so its components run before the reader opens it, which is how the Terminal plugin starts the shells of its restored terminals with the product.

A shortcut carries `id`, `keys` and `action`, and it is answered only while its view is on screen and no dialog is open:

```lua
shortcuts = {
    { id = "refresh", keys = "mod+r", action = refresh },
    { id = "next", keys = "alt+down", action = next },
},
```

The keys are modifiers followed by one key, joined by plus signs. The modifier `mod` is Command on macOS and Control on Windows and Linux, and `shift` and `alt` complete the set. A key is a letter, a digit, `f1` to `f12`, an arrow such as `down`, or one of `enter`, `escape`, `tab`, `space`, `backspace`, `delete`, `home`, `end`, `pageup`, `pagedown`, `minus`, `equal`, `leftbracket` and `rightbracket`. A combination needs `mod` or `alt` unless its key is a function key, so a shortcut never takes a character the reader is typing. A shortcut is answered even while a field of its view has the keyboard, so the combinations the core keeps are refused: quitting, the first nine destinations, the editing chords `mod+a`, `mod+c`, `mod+v`, `mod+x`, `mod+z`, `mod+y` and `mod+shift+z`, and the caret moves, `mod` with an arrow, `home`, `end`, `backspace` or `delete` and `alt` with `left`, `right`, `backspace` or `delete`, with or without `shift`.

A band carries `id`, `titleKey`, `placement` of `top` or `bottom`, `order`, `height` in points and `view`. The bands of the top stand above everything in the window and the bands of the bottom below everything, across its whole width and in the order of `order`, each apart from the rest by one divider, so the mode bar, the view and the notifications move to leave them their room. A height is zero, which hides the band while its components keep their state, or from 16 to 320 points, and anything else is refused with `shell_band_height_invalid`. Two bands claiming one order in one placement refuse the plugin declaring the second with `plugin_band_order_taken`, and a band identifier that is invalid or repeated inside its plugin is refused with `band_identifier_invalid`. A band is built as soon as the workspace is ready, and its plugin changes its height at any moment with `workpane.shell.resizeBand`:

```lua
bands = {
    { id = "adventure", titleKey = "task-hero.band.title", placement = "bottom", order = 100, height = 88, view = view.build },
},
```

A settings group carries `id`, `titleKey` and `sections`. A section carries `id`, `titleKey`, `searchKeys`, a list of at least one key whose texts the settings search also matches, or it is refused with `settings_section_untranslated`, and `view`. A group holds at least one section, or it is refused with `settings_sections_missing`, and a group of one section names it `general`, or it is refused with `settings_single_section`.

A view function is called the first time its destination, band or section is shown, and the tree it answers stays mounted afterwards, so returning to a destination preserves everything in it. The function receives a context table with the `surface` it builds, and the `item`, or the `group` and `section`, it was asked for. A view that fails to build shows a translated failure message in its place and writes the reason to the log.

## Lifecycle

Every plugin the product discovers is kept with its state, which the Installed section of the Plugins group of the settings shows beside its title:

| State | Meaning |
| --- | --- |
| `discovered` | The product found it and has not yet decided whether it starts |
| `waiting` | A plugin it depends on does not run, or the reader just turned it on, and it starts by itself once its turn comes |
| `starting` | It is being registered and its `start` runs |
| `running` | The plugin started and everything it contributes is on screen |
| `stopping` | Its dependents and its `stop` run before it is withdrawn |
| `disabled` | The reader turned it off, so it is listed and never started |
| `unavailable` | Its `enabled` function answered false |
| `refused` | It could not be read, registered or started, and the reason is kept with it |

Every change of state is written to the log at the info level under the category `plugins`, with the plugin, the state it left, the state it entered and the reason, so the Logs view tells how any case unfolded. The Plugins section shows the passing states while they last, every switch and erase button waits while any change of the plugins runs, a switch follows what the reader chose, so it never springs back while its plugin stops, and a plugin the reader turned off stays off with its switch off even when its code cannot be read, a failure told only once the reader turns it on. Closing the product waits for the change in progress before the plugins stop, so no stop runs twice or beside a start.

1. The product discovers every plugin in the folder it carries and then in the plugin folders of the settings, reads every Lua file of each one into memory and loads `plugin.lua` in an environment of its own, telling the reader about a folder it cannot read, an entry of a folder it cannot describe, such as a link to nothing, and a plugin whose identifier is already taken, while every other plugin still loads.
2. The catalog of every discovered plugin is validated and installed at once, so the Plugins group writes the title and the description of a plugin that does not run in the language of the reader, and a catalog that breaks the rules refuses its plugin.
3. The plugins are ordered by their dependencies. A plugin with a missing dependency is refused with `plugin_dependency_missing`, the plugins of a cycle with `plugin_dependency_cycle`, and a plugin whose dependency is disabled, unavailable or refused waits with `plugin_dependency_inactive`.
4. Each plugin that may run is registered, its destinations, bands and settings validated all together or not at all, and its `start` runs. A plugin whose registration or start fails is withdrawn completely. A view, a band or a settings section the shell asks of a plugin while it starts, such as a view it builds ahead of time, is built as soon as its start finishes, and one asked of a plugin that is not running is left for the shell to ask again once the plugin runs.
5. The shell leaves its loading state once every plugin had its turn, and a refused plugin is named to the reader in a notification while every other plugin keeps running.

The reader turns a plugin off with the switch of its row, and it stops at once: every running plugin that depends on it stops first, after the reader confirms a list naming them, each `stop` runs for at most three seconds, and the plugin is withdrawn. Withdrawing takes back everything the plugin registered, its subscriptions, capabilities, watchers, log subscribers, servers, programs, sounds, preference documents, open dialogs, surfaces, destinations, bands and settings, each step on its own so one that fails never keeps the others, and a destination on screen that belonged to it gives way to the first destination left. Turning a plugin on reads its folder again, so an author sees the code just written, starts it and then starts every plugin that waited for it, in the order of their dependencies. The position the reader gives each switch is kept in the core preferences, so every plugin stays as the reader left it after a restart, and a plugin that declares `offByDefault`, such as Flappy Bird and Task Hero, stays off until the reader turns it on. One change of the plugins runs at a time, so a switch pressed twice never meets itself halfway.

A plugin that is turned off and still keeps data shows a button beside its switch that erases that data after a destructive confirmation, dropping every table, index, view and trigger under its prefix, its schema version and every preference document it keeps. A plugin that is no longer installed but left data behind is listed under Removed plugins, which appears only while there is such data, with the same action. The data of a running plugin is never erased, and asking for it is refused with `plugin_running`.

A plugin publishes and subscribes to events, provides and requests capabilities, reads the log and starts tasks only from its registration until it is withdrawn, so a call made while its `plugin.lua` loads, or by code a withdrawn plugin left behind, is refused with `plugin_not_running` and leaves nothing behind. When the product closes, every running plugin stops in the reverse order it started, and a call that still reaches the host, such as one from a finalizer, is refused with `bridge_closed`.

When an enabled plugin's stored table structure differs from its current declaration or declares a newer schema, or its start reports invalid persisted rows, the manager withdraws its registration, writes a complete database backup under the private `plugin-data-recovery/<plugin>` directory and atomically resets only that plugin's tables, schema record and preference documents. It reads the current plugin again and tries one fresh start. A translated warning names the backup. If backup or erasure fails, the plugin remains refused with its old data preserved. Disabled plugins are not reset, other plugins retain their data and arbitrary startup errors do not trigger recovery. The plugin API stays the current SDK contract, with additive changes allowed and a new SDK level required for a breaking change. Recovery does not load historical data layouts.

## The environment

A plugin sees the standard Lua functions for tables, strings, math, UTF-8 and coroutines, a restricted `os` with `clock`, `date` and `time`, and three additions:

| Name | Meaning |
| --- | --- |
| `workpane` | The API of this plugin, whose every call carries the identity of the plugin |
| `include(name)` | Loads another Lua file of the same plugin once and answers what it returned |
| `print(...)` | Writes an information entry to the centralized log |

A plugin reaches the Varn modules `async`, `crypto`, `datetime`, `fs`, `http`, `json`, `platform`, `process`, `socket`, `xml` and `zip` through `require`, and never the modules of the SDK, which keep the only reference to the host.

Each plugin receives its own copy of every module it requires, of the standard library tables and of the lists of `workpane.app`, so a change it makes to one of them never reaches another plugin. The tables of the SDK are locked, and `getmetatable` answers the metatables the plugin set itself, the name of the type for a value of the SDK, such as `workpane.node`, `workpane.future`, `workpane.failure` or `workpane.preferences`, `string` for a string, nothing for a table the `json` module marked as an array or an object, which is plain data, and `workpane.protected` for any other table.

The `async` module a plugin receives starts every coroutine of `spawn` as a protected task of the plugin and has neither `run` nor `onFailure`, because the SDK holds the one handler of the runtime, which writes a failure nobody receives, such as a promise that rejected while nothing awaited it, to the log under the category `runtime`. Promises a plugin starts together are awaited together through `async.all` or `async.allSettled`, because a promise that rejects while its coroutine still awaits another one counts as one nobody awaited. A promise Varn settles on one of its worker threads, such as a read of `fs` or a request of `http`, wakes the product and resumes its coroutine at once, and a sleeping coroutine wakes it at its deadline, so a plugin waiting for either costs nothing meanwhile.

The function `http.client.exchange(options, onChunk)` returns an owned exchange with `promise` and `cancel()`. Without `onChunk`, its promise answers the buffered `{ status, headers, body }` response. With a chunk handler, it streams the body, calls `options.onResponse(status, headers)` before its chunks and settles after them. Await the promise as soon as the request starts and retain the exchange while it runs. Calling `exchange:cancel()` interrupts the active native connection, suppresses later chunks and rejects the pending promise. Cancellation after completion and repeated cancellation have no effect. Collecting a discarded exchange also cancels its request. Request timeouts and `maxResponseBytes` bound the actual transport, including buffered responses.

The function `zip.list(path, maximumEntries)` inspects an archive on a worker and refuses its entry count before allocating the listing. Its optional bound is an integer from 1 to 100000 and defaults to 100000. The rows carry `name`, `size`, `compressedSize` and `unsafe`, and an entry whose required size metadata cannot be read refuses the listing. A plugin checks the names, sizes and complete declaration before calling `zip.extract`.

## Levels of the SDK

Every plugin declares in its `sdk` field the level of the SDK it is written for, and the product refuses with `plugin_sdk_unsupported` a plugin that declares no level or any level other than the one `workpane.app.sdk` answers. The API grows by adding functions, fields and events, which every plugin of the current level may use, and a change to what an existing function takes or answers, or to the payload of an event or a capability, raises the level, so a plugin written for an earlier one is refused by name instead of failing later in a call.

Everything crosses between Lua and the host as JSON, a call answers `{ ok, value }` or `{ ok, error }` with a structured error of `code`, `message` and `detail`, and the codes are part of the API, so a plugin compares codes and never messages.

## Futures

Anything that waits answers a future. Inside a handler, a view builder or a task, a future is awaited in one of two ways:

```lua
local rows, failure = workpane.database.query("SELECT title FROM notes__items"):await()

if failure ~= nil then
    workpane.log.error("storage", tostring(failure))
    return
end

local rows = workpane.await(workpane.database.query("SELECT title FROM notes__items"))
```

The first form answers the value and a failure, and the second raises the failure. A failure is a table with `code`, `message` and `detail`, and `tostring` writes it as one line, its message followed by its code and its detail in quotes, as in `The path does not exist (code "files_path_missing", detail "/home/ana/notes.md")`. The function `workpane.isFailure(value)` tells a failure from anything else.

Work that is not already inside a handler is started with `workpane.task(work, ...)`, which runs the function with the arguments that follow it in a protected coroutine and writes a failure to the log instead of ending anything, and anything other than a function is refused with `task_work_invalid`.

The function `workpane.future(work, ...)` starts protected work with the following arguments and returns its owned future, preserving structured failures for its caller. Invalid work fails with `task_work_invalid`, and a withdrawn owner cannot start another future.

Withdrawing a plugin cancels its suspended handlers, tasks and SDK operations before releasing its resources. A cancelled future answers `plugin_not_running` and keeps that result if a native reply arrives later. A capability operation belongs to both its caller and its provider, so withdrawing either cancels the operation. Starting the plugin again does not resume the tasks of its previous instance. The protected waiters of owned staging operations finish accepted filesystem work before cleanup, even when their plugin task has been cancelled.

## Owned resource scopes

The function `workpane.resources.scope()` reserves a bounded scope for the running plugin. Its methods return futures and preserve structured failures. The scope is immutable, and an owner that has been withdrawn cannot create another one.

| Method | Meaning |
| --- | --- |
| `scope:acquire(open, close)` | Reserve resource admission before calling the opening function, await its future and own its distinct nonempty result with the closing function |
| `scope:run(work)` | Reserve operation admission and run accepted work in a protected coroutine that remains alive through owner withdrawal |
| `scope:release(value)` | Close an owned resource once and await its closing future |
| `scope:close()` | Stop admission, close existing resources before waiting for blocked operations and release any resource whose accepted opening completes late |

The opening and closing functions return futures. Accepted opening and cleanup survive cancellation of the caller. Closing is idempotent, refuses `resources_close_in_operation` when an operation tries to close its own scope and waits until every accepted operation and resource cleanup has settled. A cleanup failure is retained while every other resource is still closed, then answered to the caller. The plugin bounds its native connect and handshake waits, so a peer cannot keep accepted networking work alive indefinitely. Withdrawing the plugin starts scope cleanup before cancelling its ordinary tasks.

Mistyped callbacks fail with `resources_work_invalid`, unknown or duplicate resource identities fail with `resources_value_invalid`, admission after closure fails with `resources_closed` and a full operation, resource or plugin scope group fails with `resources_full`. Scope objects, callbacks and native resources stay inside their plugin and never cross a capability or event.

## The API

### Plugin and application

| Name | Meaning |
| --- | --- |
| `workpane.plugin.id`, `workpane.plugin.directory` | The identity and the folder of this plugin |
| `workpane.app.version`, `buildNumber`, `sdk`, `debug`, `platform`, `architecture` | What the running product is, its separate build number and the level of its SDK |
| `workpane.app.processId` | The process identity of the product, which a language server receives so it ends with the product |
| `workpane.app.dataDirectory` | The data directory of the product, where a plugin keeps files such as the shell history of its terminals |
| `workpane.app.languages`, `themes`, `icons`, `colors` | What the product offers, where the colors are the roles of the palette described in the component reference |
| `workpane.app.language()`, `workpane.app.theme()` | The language and theme selected now |
| `workpane.app.watch(handler)` | Receives `{ language, theme }` after each change the reader makes and answers the function that stops it, and anything but a function is refused with `reader_handler_invalid` |
| `workpane.app.isClosing()` | Reports whether product shutdown began, before any plugin stops, while stopping hooks retain their normal API access |
| `workpane.app.quit()` | Asks the reader to confirm quitting |
| `workpane.app.watchPlugins(handler)` | Watches copied plugin directory snapshots at each state change and returns its unsubscribe function, with protected owner-bound handlers that are removed on withdrawal |
| `workpane.app.plugins()` | Every plugin the product discovered with its `id`, `titleKey`, `descriptionKey`, whether it is `bundled` and its `state`, as copies of plain data |

### Shell

| Name | Meaning |
| --- | --- |
| `workpane.shell.navigate(item)` | Shows another destination of the same plugin |
| `workpane.shell.resizeBand(item, height)` | Changes the height of a band of the plugin to zero, which hides it, or to 16 to 320 points, and the band keeps the new height until the plugin stops |

A band this plugin does not declare is refused with `shell_band_unknown` and a height out of range with `shell_band_height_invalid`.

### Interface

The builders of `workpane.ui` create the components listed in [Components](components.md), and `workpane.ui.isNode(value)` answers whether a value is a node one of them built.

Use `ui.pathField({ mode = "file" })` or `ui.pathField({ mode = "folder" })` for editable filesystem paths. The SDK opens the native selector, with optional file filters, and reports accepted selections through change and submit. A custom `onBrowse(event, node)` receives an `event.isCurrent()` guard for its exact selection and may await a remote selector and returns a nonempty UTF-8 path or nil. `node:mounted()` checks whether its surface still owns it, including while asynchronous preparation runs. Selection never replaces a value edited while the selector was open, and a detached node receives no late result.

Any component can be dragged onto another one. The dragged component declares a `drag` value with a kind, and a target lists the kinds it `accepts` and receives the value in its `drop` event:

```lua
local card = ui.card({ drag = { kind = "notes.card", value = note.id, label = note.title } }, { ui.label({ text = note.title }) })
local done = ui.column({ accepts = { "notes.card" }, onDrop = function(event)
    move(event.value, "done")
end }, {})
```

Replacing the children of a container with `node:setChildren` keeps every node already inside it that the new children name again, wherever it lands among them. A plugin that rearranges its view therefore builds the new containers freely and passes the nodes it keeps, and a terminal or a web view among them moves with its running shell or its loaded page. Properties given as the second argument change in the same step as the children, which is how a strip of tabs changes its tabs and its pages together.

### Drawing on a canvas

A canvas draws whatever list of shapes, pictures and text its plugin sends, and ticks at a frame rate while it is on screen, which is how a game or an animation is written. A plugin keeps the state of its world in a module without drawing, advances it on each frame and answers with the list for the next one:

```lua
local canvas
canvas = ui.canvas({ grow = 1, frameRate = 60, pixelated = true, focusable = true, pictures = drawing.pictures(), onFrame = function(frame)
    game.step(state, frame.delta)
    canvas:command("draw", { commands = drawing.commands(state, frame) })
end, onKeyDown = function(event)
    if event.key == "space" then
        game.flap(state)
    end
end })
```

The commands, properties and events of the canvas are described in [Components](components.md#canvas). A drawing laid out around its words, such as a box of counters, measures each text with `workpane.ui.textWidth(text, { size, face })`, which answers its width in points for a literal or a translation reference in the language of the reader, at a `size` from 6 to 128 points in the `regular`, `semibold` or `monospace` face a canvas draws with, and refuses a size out of range with `json_field_range` and a call before the window has its fonts with `ui_measure_unavailable`. Every command crosses the bridge on every frame, so a floor or a background that repeats is drawn as one tiled picture, and a paused game sets its `frameRate` to zero so it costs nothing while it waits. A game names every picture it draws in `pictures`, so each sheet is decoded before the first frame that shows it and no figure goes missing for a moment, and the canvas lets them go when the plugin turns off. A canvas off screen, in a hidden band or in a minimized window receives no frame, so the world waits with it.

### Texts

| Name | Meaning |
| --- | --- |
| `workpane.i18n.text(key, ...)` | A text a component shows, translated again whenever the language changes |
| `workpane.i18n.translate(key, ...)` | The text in the current language, for dialogs, notifications and logs |
| `workpane.i18n.number(value, decimals)` | A number argument written with the separators of the language being read, rounded to zero to six decimals |

An argument of a sentence is a text written as it is, a number made by `workpane.i18n.number` or another sentence made by `workpane.i18n.text`, nested at most four deep. Numbers and nested sentences are written again when the reader changes the language:

```lua
local size = workpane.i18n.text("notes.unit.gibibytes", workpane.i18n.number(bytes / 1024 ^ 3, 2))
ui.label({ text = workpane.i18n.text("notes.view.free", size) })
```

The file `translations.lua` returns one table per language, and every language spells every key:

```lua
return {
    en = {
        ["notes.plugin.title"] = "Notes",
        ["notes.view.count"] = "%1 notes",
    },
    pt = {
        ["notes.plugin.title"] = "Notas",
        ["notes.view.count"] = "%1 notas",
    },
}
```

A key has three lowercase parts of letters, numbers and hyphens, and the first part is the plugin identifier. Arguments are written `%1` to `%9`, and every language declares the same ones.

### Preferences

A plugin declares the values it keeps with a schema of rules, and reads and writes them through the store it answers:

```lua
local store = workpane.preferences.define({
    sort = { type = "string", default = "newest", choices = { "newest", "oldest" } },
    pageSize = { type = "integer", default = 50, minimum = 10, maximum = 500 },
    wrap = { type = "boolean", default = true },
    servers = { type = "list", maxItems = 16, items = { type = "record", fields = {
        name = { type = "string", default = "", maxLength = 80 },
        port = { type = "integer", default = 8080, minimum = 1, maximum = 65535 },
    } } },
})

local size = store:get("pageSize")
store:set("wrap", false)
```

| Type | Fields besides `type`, `default` and `check` |
| --- | --- |
| `boolean` | None |
| `integer`, `number` | `minimum` and `maximum`, where a number is finite and an integer written as a whole float reads as an integer |
| `string` | `choices`, a list of distinct texts, and `maxLength` in bytes |
| `list` | `items`, the rule of every item, `maxItems` and `unique` |
| `record` | `fields`, a table of named rules, where a field left out takes its default and an undeclared one is refused |
| `map` | `values`, the rule of every value under a text key, and `maxEntries` |
| `any` | None, and the value is any plain data of texts, finite numbers, booleans and tables of them |

Every rule needs its `default`, except a list, a record and a map, which start empty, and the item of a list and the value of a map, which are never missing. A `check` is a function answering true for a value the plugin can use, such as an address it accepts, and it sees a copy of the value. Rules nest to any depth. A rule with a field its type does not take, a field of the wrong type, choices that are not distinct texts, bounds that are not finite or form no range, or a default its own rule refuses is refused with `preferences_rule_invalid` and the path of the field in `detail`, such as `servers[].port`.

| Method | Meaning |
| --- | --- |
| `store:get(key)` | A copy of the value of a preference |
| `store:values()` | A copy of every value |
| `store:set(key, value)` | Changes one value at once and answers a future that settles once the document is durable |
| `store:update(changes)` | Changes several values together, all of them or none, and writes the document once |
| `store:reset(key)` | Puts back the default of one preference, or of every one when no key is named |
| `store:watch(key, handler)` | Runs the handler with a copy of the new value after each change of the key, or with the value and the key after each change of any key when only a handler is given, and answers the function that stops it |
| `store:control(key, properties)` | Builds the control that edits the preference, described below |

A value is validated whole, and one its rule refuses raises `preferences_value_invalid` with the path of the first problem in `detail`, while a key the schema does not declare raises `preferences_key_undeclared`. A change applies in memory at once, so every reader and watcher sees it before it is stored, and each watcher runs in a task of the plugin. The watchers of a change are taken before the first one runs, so a watcher that stops itself or adds another never changes who hears that change, which holds for the subscribers of an event and the watchers of a capability and of the reader too. A write that fails puts the whole document it last stored back, tells the watchers of every value that went back, writes the failure to the log and fails the future of the write, while the failure of an older write leaves a newer one in charge.

The stored document is read when the schema is defined, and a stored value its rule refuses reads as its default and is written to the log of the plugin once as a warning, while a stored key the schema no longer declares is left out of the next write. So a schema changes freely from one version of a plugin to the next without any migration.

A plugin keeps several documents by naming them, as in `workpane.preferences.define(schema, { document = "layout" })`, and a name follows the grammar of identifiers or is refused with `preferences_document_invalid`. Each document is defined once while its plugin runs, and a second definition is refused with `preferences_defined`. A plugin that stops leaves its documents, which it defines anew when it starts again, and its watchers never run after it stopped.

A control is bound to its preference: it shows the value, writes what the reader changes and follows every change from anywhere else. Its properties are those of the component it builds, and `as` chooses the component where a rule has more than one:

| Rule | Components, the first being the default |
| --- | --- |
| `boolean` | `toggle`, `checkbox` |
| `integer`, `number` | `numberField`, `slider`, with the bounds of the rule, and whole steps for an integer |
| `string` with `choices` | `combo`, `radioGroup`, whose `labels` table gives the text of every choice |
| Any other `string` | `textField`, `pathField` and `secretField`, which write when the reader leaves them or presses Enter, `textArea`, `combo` with the `options` its properties give |

```lua
ui.settingsRow({ label = text("notes.settings.sort") }, store:control("sort", { labels = { newest = text("notes.sort.newest"), oldest = text("notes.sort.oldest") } }))
```

A value the reader enters that the rule refuses puts the stored value back in the control, and a control nobody holds any more, such as one of a dialog that closed, stops following its preference. A control for a rule without a component of its own, a component that does not fit the rule or a choice without its text is refused with `preferences_control_invalid`, a watcher that is not a function with `preferences_watcher_invalid`, a schema that is not a plain table, options that are not a table or an option other than `document` with `preferences_schema_invalid`, and a store method called on anything but a store with `preferences_store_invalid`.

### Database

Every plugin has scoped tables in the shared SQLite database. A table name starts with the plugin identifier, with hyphens written as underscores, followed by two underscores, such as `notes__items` for the plugin `notes`, so the tables of `notes` and of `notes-archive` never share a prefix.

| Name | Meaning |
| --- | --- |
| `workpane.database.query(sql, bindings)` | Answers the rows as tables keyed by column |
| `workpane.database.run(sql, bindings)` | Answers `changes` and `lastInsertRowId` |
| `workpane.database.transaction(statements)` | Runs statements given as `{ sql, bindings }` together or not at all |
| `workpane.database.null` | The value that binds SQL NULL, because a Lua list cannot hold nil |
| `workpane.database.invalid(detail)` | Raises `database_rows_invalid` for stored rows that break the rules of the plugin, which triggers one complete backup and reset before a fresh start |

The `migrations` of the definition are applied before every start of the plugin, each list after the stored version once and atomically, so its `start` already finds its tables as its code expects them, and a migration that fails refuses the plugin with its code. Since the definition is read when the plugin is discovered, the product knows the newest schema every installed plugin reads even when it never ran here or is turned off, which is what an imported configuration is proven against.

Every call answers a future and runs on the database thread. A plugin creates, changes and drops its own tables, indexes, views and triggers only in its migrations, where a view and a trigger carry the prefix of the plugin, a trigger watches only a table of its plugin, a column can be added, renamed or dropped and a foreign key refers only to a table of the plugin, or the migration is refused with `database_foreign_key_foreign`. A name the plugin gives, even a common table expression of a query, starts with its prefix in lowercase, since a name written in other letters is never the plugin's own. A statement reaching a table of another plugin or of the core, the schema itself, a pragma, an attached database or a transaction command is refused, and renaming a table is refused because it could leave the namespace of the plugin. A statement runs for at most ten seconds and a migration for at most two minutes, and one that runs longer, or is still running as the product closes, is interrupted with `database_interrupted`, so no plugin holds the database of the others. A binding is a finite number, a text, a boolean or `workpane.database.null`, and a number that is not finite is refused with `database_binding_invalid`. A migration list declares the current structure. A stored version newer than the list is refused by storage and the manager applies its bounded backup and reset policy. Before the first stable release, bundled plugins create their current schema directly without historical steps that create and then remove unused columns or indexes. At every start the tables, indexes, views and triggers the plugin keeps are compared both ways with the ones its migrations create in a database in memory, ignoring spacing and quotes, so an object the migrations no longer create refuses the plugin as surely as one they create differently. A difference refuses the plugin with `database_schema_mismatch` naming the object, which the reader is told as stored data that could not be read, with the place of the file, and the reader can then erase the data of the plugin from the Plugins section. A recorded schema newer than the installed declaration is explained there too, so recovery remains available when the Logs plugin itself cannot start. Erasure requires destructive confirmation and removes only that plugin's tables, schema record and preference documents. After a successful erase, an enabled installed plugin is read and started again, and its waiting dependents resume when their dependencies run. A disabled or removed plugin stays stopped. Cancellation and storage failure preserve the data and never start the plugin.

### Events and capabilities

| Name | Meaning |
| --- | --- |
| `workpane.events.publish(topic, payload)` | Delivers a payload to every other plugin subscribed to the topic, and the topic starts with the plugin identifier |
| `workpane.events.subscribe(topic, handler)` | Receives the payload and sender and returns an idempotent unsubscribe function |
| `workpane.capabilities.provide(name, handler, contract)` | Answers requests for a capability with a handler that receives the payload and the identifier of the plugin asking, and a second provider of one name is refused with `capability_provided` |
| `workpane.capabilities.list()` | Every capability a running plugin provides, ordered by name, each with its `name`, its `provider`, the `summaryKey` of its contract, the schema of its `payload` and whether `agents` may ask for it |
| `workpane.capabilities.request(name, payload)` | Answers a future of the value the provider returned or the failure it raised, and fails with `capability_unavailable` when no plugin provides it |
| `workpane.capabilities.available(name)` | True while a running plugin provides the capability, so an action it needs is offered only when something answers it |
| `workpane.capabilities.watch(name, handler)` | Runs the handler with whether a provider answers each time one appears or leaves, as plugins are turned on and off, and answers the function that stops it |

The contract of a capability is `{ summaryKey, payload, agents }`, where `summaryKey` is a key of the catalog of the provider that says in one sentence what the capability does and `payload` is a JSON schema of type `object` that describes what a request carries, and `agents`, optional and false when left out, is true for a capability an agent of the AI plugin may ask for, which only one that reads or shows something without erasing anything or reaching outside the folder of the agent declares. A contract that is missing, names a key of another plugin, describes anything but an object or gives `agents` as anything but a boolean is refused with `capability_contract_invalid`, so every capability tells a plugin or an agent what it answers before anything asks it:

```lua
workpane.capabilities.provide("notes.page.open", openPage, {
    summaryKey = "notes.contract.page-open",
    payload = { type = "object", properties = { path = { type = "string", description = "Absolute path of a note" } }, required = { "path" } },
    agents = true,
})
```

An event unsubscribe function keeps its registration identity when another plugin is withdrawn. Calling it more than once removes only that registration and leaves other subscribers and later registrations intact.

Topics and capability names follow the grammar of translation keys. A handler that is not a function is refused when it is given, with `event_handler_invalid` or `capability_handler_invalid`. A payload and the value a provider answers cross between plugins as copies of plain data, so every subscriber receives a copy of its own, and a function, a userdata, a table with a metatable other than the mark of the `json` module or a cycle among them is refused with `event_payload_invalid` or `capability_value_invalid`.

### Dialogs and notifications

| Name | Answer |
| --- | --- |
| `workpane.dialogs.confirm({ title, message, detail, confirmText, cancelText, destructive })` | True only when the reader confirmed |
| `workpane.dialogs.alert({ title, message, detail, confirmText })` | Nothing, once closed |
| `workpane.dialogs.prompt({ title, message, value, placeholder, confirmText, cancelText })` | The text, or nil when cancelled |
| `workpane.dialogs.custom({ title, message, content, buttons, width, onButton })` | The identifier of the button that closed it |
| `workpane.dialogs.openFile({ title, initial, filters, multiple })` | The chosen paths, empty when cancelled |
| `workpane.dialogs.selectFolder({ title, initial, backend, workspace, isCurrent })` | The chosen folder, or nil, from a native dialog or the selected workspace |
| `workpane.dialogs.saveFile({ title, initial, filters })` | The chosen path, or nil |
| `workpane.dialogs.message({ title, message, kind, buttons })` | The button the reader pressed |

Every dialog answers a future. A filter is `{ name, patterns }`, a message kind is `information`, `warning`, `error` or `question`, and message buttons are `ok`, `ok-cancel`, `yes-no` or `yes-no-cancel`. The title, message, detail and button texts of a dialog are strings written with `workpane.i18n.translate`, because a dialog shows them only while it is open. A custom dialog shows its `content` tree, and its components keep their values after it closes, so the plugin reads them from the nodes. Its buttons sit under the content and carry `id`, `text` and the optional `variant`, `icon`, `enabled` and `closes`. A button with `closes = false` keeps the dialog open and calls `onButton(id, dialog)` in a task of the plugin, which checks its form and calls `dialog:close(id)` once it is valid, and `dialog:setButtons(buttons)` replaces the buttons while the dialog is open, so they follow the state of what it shows. Escape and `mod+w` close any dialog with `cancel`, after Escape first closes a list or a menu opened inside it.

A dialog takes the height its content needs when it opens, bounded by the window, and keeps it: its title and its buttons stay in place and the content scrolls between them clear of the scroll bar, so a problem that appears or a page that switches never resizes it and the bar never covers a control. A form lays out each field with `ui.formField`, which writes the label above its control followed by a colon, and a checkbox keeps its own text without a label beside it. A problem the reader must fix before a form can be saved, such as a field that is missing, is shown with `ui.alert` inside the form rather than with a colored label, so every problem of the product looks the same. An alert takes a `tone` for a warning, a success or a note, and text a plugin writes over a colored fill uses the ink of that fill, such as `on-warning` over `warning`.

A custom dialog may declare `defaultButton` as the identifier of one of its initial buttons. Enter and keypad Enter invoke that enabled button through the same action as a click, including its `onButton` validation when `closes` is false. Multiline input, another active control, modified keys and a nested menu keep their own keyboard behavior. A disabled or removed default button is not invoked. Other dialog kinds refuse this property.

A dialog opened while another one is on screen is drawn above it and answered first, so a form can ask for a confirmation without closing. The dialogs of a plugin that stops close as cancelled, so a coroutine waiting on one of them ends instead of waiting forever.

A folder selector with `backend` navigates that existing workspace through its asynchronous `canonical`, `stat` and `list` functions, within `backend.identity.root`. It lists only directories inside its own scroll area, bounds each listing to 2000 entries, reports partial listings as selectable warnings and unavailable folders as errors and cancels on disconnect. A standalone Windows drive designator in its address is normalized to its drive root before lookup. A selector with `workspace` acquires a workspace from its exact endpoint descriptor and closes it after selection, cancellation or failure. Supply `isCurrent` to prevent a modal opening after its originating field was detached during connection preparation. Listing and final selection show a loading indicator. Navigation and Select wait for the accepted request, while Cancel remains available and replies after closure make no UI changes. A remote selector never invokes a local native folder dialog.

The product notifications are `workpane.notify.information`, `success`, `warning` and `error`, each taking a title and a message. At most four are shown at once, none is shown over the loading screen, and each closes six seconds after the workspace first shows it, so a notice posted while the plugins start is still read in full. The operating system notification is `workpane.notify.system(title, message, kind)`.

### Sounds

| Name | Meaning |
| --- | --- |
| `workpane.audio.play(path, { volume, loop })` | Plays a WAV, FLAC or MP3 file of the `assets` folder of the plugin at a volume from 0 to 1, once or in a loop, and answers the identity of the sound |
| `workpane.audio.stop(sound)` | Stops a sound of the plugin |
| `workpane.audio.stopAll()` | Stops every sound of the plugin |

A sound starts at once without waiting for its file, which the audio thread opens and decodes in the background, and that thread opens the audio device the first time a sound is asked for. A path outside the assets is refused with `audio_path_invalid`, a file that is missing with `audio_file_missing`, another format with `audio_format_unsupported`, a volume out of range with `audio_volume_invalid`, options that are not a table with `audio_options_invalid` and a sound that the plugin is not playing with `audio_sound_unknown`. A machine without an audio device, or a file that cannot be decoded, still answers every call, and the sound ends at once without being heard, which the log tells once for the missing device and once per file in the log of the plugin. The sounds of a plugin stop when it stops.

### Logs, system and time

| Name | Meaning |
| --- | --- |
| `workpane.log.debug`, `info`, `warning`, `error` | Write `(category, message, details)` to the centralized log |
| `workpane.log.subscribe(handler)` | Receives every entry of the centralized log, and the first subscriber also receives what was written before anyone listened |
| `workpane.system.openUrl(url)` | Opens an HTTP or HTTPS address in the default browser and answers a future, refusing any other address with `system_url_refused` |
| `workpane.system.revealPath(path)` | Shows a file or folder in the file manager and answers a future, refusing malformed UTF-8, NUL bytes and paths that are not absolute with `system_path_invalid` |
| `workpane.system.copy(text)`, `workpane.system.paste()` | Writes and reads the text of the system clipboard |
| `workpane.system.home()` | The home directory of the reader, where a new terminal or a folder picker starts |
| `workpane.system.shell()` | The default shell a terminal component starts, as `{ name, path }`, whose name such as `zsh` or `pwsh` names a new terminal |
| `workpane.system.information()` | A future of a snapshot of the machine, collected on a worker in about a fifth of a second |
| `workpane.system.monospaceFonts()` | A future of the names of the monospaced families installed on the machine in alphabetical order, listed once on a worker and offered as the `fontFamily` of a code editor or a terminal |
| `workpane.time.now()` | The current moment as a stored UTC timestamp |
| `workpane.time.localPresentation(timestamp)` | The moment written in the local zone of the reader |
| `workpane.time.zone()` | The time zone the system is set to, named as the time zone database names it, such as `Europe/Lisbon` |
| `workpane.time.offset(zone, seconds)` | The offset in seconds from UTC that a zone has at an instant given in seconds since the epoch, raising `time_zone_unknown` for a zone the system does not know |

Entries reach the subscribers of the log once per frame, each in a protected task. A handler that is not a function is refused with `log_handler_invalid`, a subscriber that fails is removed after its first failure, which is written once, and a subscriber that writes to the log while it reads an entry is refused with `log_write_in_delivery`, so no subscriber feeds itself. A removed or withdrawn subscription becomes inactive, so backlog delivery stops calling it and handlers already awaiting an operation cannot report its failure again. A path given to `revealPath` must be absolute UTF-8 without NUL and fails with `system_path_invalid` otherwise. The SDK validates its original text before serialization and the host validates it before constructing a native path, so malformed text never reaches the file manager. Every path the host answers, such as the home directory, is written with forward slashes on every platform, where a network share of Windows starts with two of them.

A snapshot of the machine is a table with these fields, where an unknown text is empty and an unknown quantity is zero:

| Field | Contents |
| --- | --- |
| `capturedAt` | The stored UTC timestamp of the collection |
| `os` | `hostName`, `name`, `version`, `kernel`, `architectureBits` and `byteOrder`, which is `little-endian` or `big-endian` |
| `processors` | A list of `vendor`, `model`, `physicalCores`, `logicalCores`, `flags` and `cores`, each core with `id`, `l1Data`, `l1Instruction`, `l2`, `l3`, `maximumFrequency` in hertz and `smt` |
| `processorUsage` | `utilization` from 0 to 1 and `threads`, each with `utilization` and `frequency`, or an empty table where the platform cannot sample |
| `memory` | `total`, `free`, `available` in bytes and `modules`, each with `vendor`, `name`, `model`, `serial`, `size` and `frequency` |
| `graphics` | A list of `vendor`, `name`, `driver`, `vendorId`, `deviceId`, `dedicatedMemory`, `sharedMemory`, `frequency` and `cores` |
| `displays` | A list of `name`, `width` and `height` in screen coordinates, `scale`, `density` in dots per inch, `refreshRate` and `primary` |
| `mainboard` | `vendor`, `name`, `version` and `serial` |
| `disks` | A list of `vendor`, `model`, `serial`, `interface`, `size` and `volumes`, each volume with `mountPoint` and `free` |
| `batteries` | A list of `vendor`, `model`, `serial`, `technology`, `state` and an optional `capacity` from 0 to 1, where the state is `charging`, `discharging`, `not-charging` or `unknown` |
| `networkInterfaces` | A list of `index`, `description`, `mac`, `ipv4` and `ipv6` for every interface that is up and carries an address |

### Serving folders over HTTP

| Name | Meaning |
| --- | --- |
| `workpane.http.serve({ host, port, root, onRequest })` | A future of `{ server, root }` once the server listens, where `root` is the canonical folder it serves |
| `workpane.http.stop(server)` | Stops a server of the plugin at once and releases its port |

A server is started with a table whose `onRequest` is a function when given, and anything else is refused with `http_options_invalid`. A server answers `GET` requests for the files under its root, with the index of a folder for a folder, and refuses everything else. The host is a numeric IPv4 or IPv6 address without NUL and the port is from 1 to 65535, where any other port is refused with `json_field_range`. The root is an absolute UTF-8 path without NUL and names a readable folder. An invalid root fails with `http_root_invalid`, a host with `http_host_invalid` and a port another program holds with `http_bind_failed`.

Request paths are percent-decoded and validated as UTF-8 without NUL before constructing a native path. Malformed encodings receive HTTP 400, while a valid path naming no readable file under the root receives HTTP 404.

The `onRequest` handler receives the requests of a moment together, oldest first, each with `timestamp`, `method`, `path`, `status`, `durationMs`, `responseBytes` and `remoteAddress`. The servers of a plugin stop when the plugin stops or is withdrawn.

### Reading folders

| Name | Meaning |
| --- | --- |
| `workpane.files.list(path, { maximum })` | A future of `{ entries, complete }` up to `maximum` inspected entries, each with `name`, `kind` of `directory`, `file`, `symlink` or `other`, and the `size` of a file |
| `workpane.files.stat(path)` | A future of metadata with `isFile`, `isDir`, `isSymlink`, `size` and local epoch `mtime`, or nil for a missing path, while other metadata failures remain structured errors |
| `workpane.files.hash(bytes)` | A future of the SHA-256 digest of bounded binary bytes, decoded and hashed on a worker |
| `workpane.files.walk(root, { maximum, skip })` | A future of `{ paths, complete }`, the files under the folder relative to it with forward slashes, hidden ones included, up to `maximum` |
| `workpane.files.search(root, { text, maximumMatches, maximumFileBytes, skip })` | A future of `{ matches, complete }`, each match with `path`, `line` from one and the trimmed `text` of the line |
| `workpane.files.access(path)` | A future answering `readable` and `writable` for the account running the product, for a folder as for a file, or failing with `files_path_missing` |
| `workpane.files.canonical(path)` | A future of the path with every link and dot resolved |
| `workpane.files.replace(source, destination)` | A future settled once the file written at `source` was moved over `destination` with the permissions of the file it replaces, creating the destination when it is gone |
| `workpane.files.read(path, maximum)` | A future of `{ bytes, complete }`, reading at most `maximum` binary bytes on a worker and checking one further byte to distinguish an exact complete read from a truncated one |
| `workpane.files.digest(path, maximum)` | A future of the lowercase SHA-256 digest, reading a regular file in bounded chunks up to `maximum` bytes within the host file bound and cancelling when the product stops |
| `workpane.files.staging(path)` | A future of an owned staging object with read-only `path`, after exclusive private directory creation on a worker |
| `staging:run(work)` | A future that starts the file operation returned by `work`, retains it through plugin withdrawal and waits for it before cleanup |
| `staging:close()` | An idempotent future that refuses new work, waits for accepted operations and removes only the directory this staging object created |
| `workpane.files.paths(platform).normalizeRoot(path)` | Normalizes a standalone Windows drive designator to its absolute drive root and preserves other text before validation |
| `workpane.files.absolute(path)` | Whether a path is absolute on the platform the product runs on: from the root on macOS and Linux, from a drive or a network share such as `//server/share` on Windows |
| `workpane.files.uri(path)` | The `file:` address of an absolute path, with every byte outside the unreserved set encoded, the slash a Windows drive needs and the server of a network share as its host, refusing a relative path with `files_path_invalid` |
| `workpane.files.paths(platform)` | Returns `absolute(path)`, `normalizeRoot(path)`, `uri(path)` and `path(uri)` using the declared platform, for addresses of a workspace on another machine, and refuses an unsupported platform with `files_platform_invalid` |
| `workpane.files.path(uri)` | The path a `file:` address names, or nil for any other address, where a Windows drive loses the slash before it and another server is a network share on Windows |

File address conversion uses the backend platform in both directions. A literal backslash in a Linux or macOS filename is encoded as `%5C` and restored as part of that filename. A POSIX path whose first directory resembles a drive designator keeps its leading slash. Windows paths normalize their separators and retain drive and network share addressing.

The Code Editor uses the forward slash separators of backend paths when reading their basename and parent. A backslash in a POSIX filename remains part of the name shown in the workspace and sent to the language server. Atomic save staging is created beside that file's actual parent.

Each function that answers a future runs on a worker, and every path is absolute UTF-8 without a NUL byte. A malformed path is refused with `files_path_invalid` before a native operation can read or replace its truncated prefix. The three that read and write paths answer at once, and a plugin judges and converts paths only through them, so every plugin follows the same rule the host checks again. A walk and a search never follow a linked folder and leave out the folders named in `skip`, such as `.git`, and leave out an entry they cannot read, and `complete` is false when the bound stopped them or the product quit before they finished. A search compares ASCII letters without regard to case, reads only files no larger than `maximumFileBytes` that hold UTF-8 text without a NUL byte, and cuts the text of a match at 400 characters. Walks and searches bound inspected entries, leave inaccessible subfolders out and report incomplete results while retaining readable siblings. A folder that cannot be read fails with `files_directory_unavailable`, a relative path with `files_path_invalid`, a path that does not exist with `files_path_missing`, more than 64 folders in `skip` with `files_skip_too_long` and a search text longer than 1000 bytes with `files_search_text_too_long`. The `fs` module of Varn remains the way to read, write, rename and remove files.

A folder listing requires a positive integer `maximum` within the host entry bound. The bound counts inspected entries, including one whose metadata could not be read, and a link is answered as a link without reading its target. The result holds `entries` and `complete`, which is false when metadata failed, iteration could not finish, the bound left entries unread or shutdown interrupted the operation. An exact bound that reaches the end is complete. Invalid options fail with `files_listing_options_invalid`, and unknown fields, invalid types and out of range counts are refused before scheduling any filesystem work. This result shape belongs to the current SDK level, which every bundled plugin declares.

Staging reserves its native ownership before creating its directory, so withdrawing a plugin while creation is pending still removes the accepted result. Creation neither reuses an existing path nor creates missing parents and fails with `files_private_directory_failed` without a raw filesystem diagnostic. The directory has mode 0700 on POSIX and a protected owner access list on Windows. Every operation accessing it is started inside `staging:run(function() return operation end)`, which admits at most 64 concurrent operations and keeps their protected waiters alive until they settle. The native host retains at most 32 staging directories, including those being removed. Closing is idempotent, refuses new work with `files_staging_closed` and removes only a successfully created directory. An operation attempting to close its own scope, directly or through an inherited SDK future, fails with `files_staging_close_in_operation` instead of awaiting itself. Plugin withdrawal closes its staging objects automatically. Product shutdown joins accepted runtime filesystem work before the native host removes any remaining staging directory. The current SDK declares this ownership contract.

### Connected workspaces

The function `workpane.workspace.open({ open, request, close, changed, profile, session, closedCodes })` returns a future of a connected filesystem and process backend. Its endpoint names the capabilities that reserve, use and release one consumer workspace, the event that reports its disconnection and the transport failures that invalidate it. These names carry three components separated by dots. The profile and connection identity are copied before preparation, and the backend is published only after its canonical root is inspected as a directory on its declared platform.

The backend carries `identity` with `workspace`, `profile`, `session`, `root`, `platform` and `name`, exposes that remote `platform` directly and changes `available` to false on disconnection or provider withdrawal. It exposes platform paths through `paths`, `uri` and `path`, and answers filesystem operations through futures:

| Method | Result |
| --- | --- |
| `stat(path)` | File metadata, or nil when the path is unavailable |
| `read(path)` | Binary `bytes` and the accepted content `revision` |
| `readFile(path)` | Binary file bytes |
| `readBounded(path, maximum)` | A binary prefix and explicit `complete`, without granting a revision for a partial read |
| `save(path, bytes, revision)` | The canonical path and new revision after atomic replacement, refusing external changes |
| `writeFile(path, bytes)` | Creates a file only when its destination does not exist |
| `canonical(path)` | The canonical remote path |
| `mkdir(path)`, `rename(source, destination)`, `removeRecursive(path)` | The inspected operation result, without replacing a competing destination |
| `list(path, { maximum })` | Bounded `entries` and explicit `complete` |
| `walk(path, { maximum, skip })` | Relative `paths` and explicit `complete` |
| `search(path, { text, maximumMatches, maximumFileBytes, skip })` | Bounded `matches` and explicit `complete` |
| `request(method, parameters)` | A declared protocol operation with copied plain parameters |
| `context()` | A copied native account record with `account`, `home`, `repository` and `root`, validated against this workspace and cached only while it remains connected |
| `inspect(scope)` | A read-only inspector for `home` or `repository`, exposing bounded `stat`, `canonical`, `list` and `readBounded` without granting writes or program execution |
| `close()` | An idempotent future that invalidates the backend and joins provider cleanup |

The process adapter `backend.process` exposes `find(program, directories)` and `start({ program, directory, arguments, variables, cleared, input, onOutput, onExit })` as futures. Search directories and the working directory must be absolute on the remote platform. Arguments, environment and traversal options are validated and copied before asynchronous work begins. Binary input, including an empty input that closes the stream, crosses the protocol in canonical Base64, and omitting input keeps it open for `write(process, bytes)`. Ordered `onOutput(chunks)` uses the streams `output` and `error`, and `onExit(code, failed, problem?)` follows the final output. The optional structured `problem` distinguishes a failed transport or closed workspace from a native program exit. The methods `write` and `stop` accept only a live operation from that adapter, with bounded serial writes and idempotent stopping. Closing its workspace releases the provider's entire native program group.

A malformed identity whose reserved workspace token can still be read is released before preparation fails. A root that is unavailable or is not a directory also closes its reserved scope. Binary prefix reads inspect one additional byte, so an empty file and a file exactly reaching the requested bound are complete, while a longer file remains explicitly partial. Full reads retain the content revision needed for atomic replacement.

Malformed endpoints fail with `workspace_endpoint_invalid`, invalid filesystem calls with `workspace_request_invalid`, invalid launches with `workspace_process_invalid` and malformed replies with `workspace_response_invalid`. Work attempted through an invalidated backend fails with `workspace_closed`. A consumer can assign `backend.disconnected` to respond once to lost ownership. The endpoint remains generic SDK code, while plugins choose their transport capabilities and own feature discovery and presentation.

## Credentials

The API `workpane.credentials` stores credentials for its owning plugin in the account's native protected credential service. Use it only after the reader chooses to save the credential. The host keeps each plugin in a distinct service namespace and runs operations on the worker pool.

| Function | Answer |
| --- | --- |
| `workpane.credentials.read(name)` | A future of the stored text, or nil when the entry does not exist |
| `workpane.credentials.write(name, value)` | A future that completes after the native store accepts the value |
| `workpane.credentials.erase(name)` | A future that completes after the entry is removed, including an entry already absent |

A key contains bounded ASCII letters, digits, dots, colons, underscores and hyphens. A saved value is bounded nonempty UTF-8 text without NUL or line breaks. Invalid requests answer `credential_request_invalid` without including their value. Invalid stored text answers `credential_value_invalid`. An unavailable service answers `credential_store_unavailable`, and a dismissed native unlock prompt answers `credential_store_locked`. These failures never reset plugin preferences or stop another plugin. Credentials stay outside preference documents and configuration exports, and a plugin removes its entries when the reader removes their corresponding configuration. The native implementations use macOS Keychain, Windows Credential Manager and the Linux desktop Secret Service. A Linux session must provide that service and an available default collection.

## Running programs

| Name | Meaning |
| --- | --- |
| `workpane.process.start({ program, arguments, directory, variables, cleared, input, binaryOutput, pullOutput, terminal, onOutput, onExit })` | Starts a program at once and answers its identifier, and a program given `input` reads those binary bytes and then the end of its input, so an empty one ends its input from the start and a program that would wait for input finishes instead, while one given none keeps its input open for `write` |
| `workpane.process.read(process, maximum)` | Answers a future of ordered binary `chunks`, each with `stream` and `text`, and `ended`, with `code` and `failed` once all output is drained, for a program selecting `pullOutput` |
| `workpane.process.resize(process, columns, rows)` | Changes the bounded dimensions of a running terminal program owned by this plugin |
| `workpane.process.write(process, bytes)` | Sends binary bytes to the standard input of a program |
| `workpane.process.endInput(process)` | Ends standard input after its accepted bytes are delivered and refuses further writes, while output and exit continue normally. Terminal programs refuse this operation |
| `workpane.process.stop(process)` | Asks a program to end, and ends it with everything it started two seconds later |
| `workpane.process.find(name, directories)` | A future of the absolute path of an executable found on the search path or in the added directories, or nil |
| `workpane.process.askpass({ onPrompt, onCancel })` | Creates a private credential session with process environment `variables` and a `close()` future |

A program is started with a table whose `onOutput` and `onExit` are functions when given, and anything else is refused with `process_options_invalid`. A program is named by an absolute path and starts without a shell, in an absolute directory, with the arguments as they are given, the inherited environment without the `cleared` names and with the `variables` added. Paths, arguments and environment text are UTF-8 without NUL, while standard input carries arbitrary bytes. The argument, variable and cleared-name lists, each text value and the initial input keep the launch bounds. Invalid paths, text, names or launch list sizes are refused with `process_launch_invalid` before native execution. Binary input exceeding its byte bound is refused with `process_input_too_large`. A path that is not an executable fails with `process_program_missing`, a missing directory with `process_directory_missing`, and input a program can no longer take with `process_input_refused`. On Windows a batch file runs through the command prompt.

An executable search bounds its UTF-8 name and added directories and refuses NUL or malformed text with `process_find_invalid` before native lookup. A found executable always has an absolute path, including when the system search path or an added directory is relative. The current SDK level declares these bounded launch and search contracts, and every bundled plugin targets that level.

The `onOutput` handler receives what arrived since the last turn of the loop as a list of chunks in order, each with `stream`, which is `output` or `error`, and `text`. A program selecting the boolean `binaryOutput` receives each original byte, including NUL and invalid UTF-8, through an encoded internal JSON transport. A text stream is UTF-8 with a character split between two reads kept whole and anything else replaced by U+FFFD. A handler that reads a protocol from them keeps its buffer without waiting, so the chunks stay in order. The `onExit` handler receives the exit code after the last output and a `failed` boolean, which is true for a nonzero status or a native process failure. A POSIX signal contributes its shell status of 128 plus the signal, and a Windows exception contributes its native signed status. An ordinary nonzero exit remains an exit result, so callers decide what that status means for their command. The programs of a plugin end when the plugin stops or is withdrawn.

A program selecting `pullOutput` reads exact bytes through `workpane.process.read` instead of `onOutput`. One reader waits for output or exit without polling, and its bounded byte count defaults to the native read bound. An unread producer remains blocked behind native byte and fragment bounds, including the retained prefix of a partially read chunk. The final read carries the exit only after the last byte. Plugin withdrawal releases its accepted reader and drains discarded output so every producer can finish.

Stopping a pull program cancels its unread output and releases its producer without requiring another read. An already accepted reader waits for the native exit and receives an empty final read with that exit status. Its exit handler also receives the native completion. Closing a terminal program starts native terminal cleanup immediately even when its output producer is blocked.

A program selecting `terminal = { columns, rows }` starts its executable and exact arguments behind the native pseudo-terminal with its declared environment. It keeps input open for interactive `write` and refuses an initial `input`. The terminal combines the program's output streams, and `resize` sends its new dimensions to the native terminal. Its owned wake notification delivers output and exit without idle polling. Program launch does not add shell profiles, prompt integration or history commands.

The process host bounds output waiting for delivery and pauses the serving threads while that buffer is full. Delivery wakes the producers without discarding output, and host shutdown closes the mailbox and releases every producer before waiting for the processes to end.

Native program exit is observed independently of output pipe closure. A running program that closes its output streams can still read input, while a program that exits cannot be kept active indefinitely by a descendant holding its output streams. Final output drains within the native grace before the remaining owned process group ends and `onExit` receives the original program status.

A credential session supplies `SSH_ASKPASS`, `SSH_ASKPASS_REQUIRE`, `WORKPANE_ASKPASS_PORT` and `WORKPANE_ASKPASS_TOKEN` in its `variables`, which the plugin passes to the program requesting credentials. The `onPrompt(prompt, kind)` handler may await the plugin's dialog and returns a single line of text, or nil to cancel. Its `kind` is `input`, `confirm` or `none`, where `confirm` accepts only `yes` or `no` and `none` describes an informational prompt. Use a password field for credential input. The optional `onCancel` handler closes an outstanding dialog when the session closes or its plugin is withdrawn. Never store or log credentials in preference documents or diagnostics, and never persist the session token. Save an explicitly requested password only through `workpane.credentials`. The session listens only on loopback, authenticates each request in constant time and refuses concurrent prompts, oversized data, malformed replies and late answers. The bundled Remote SSH connection clears its masked input before accepting or cancelling a credential prompt, and declining the prompt closes that connection. The packaged `WorkpaneAskpass` helper writes only an accepted answer to its standard output and returns failure without printing a response body or diagnostic. An invalid session declaration raises `askpass_options_invalid`, and a closed session's `close()` future still completes.

## Capabilities of the bundled plugins

The bundled plugins answer these capabilities, which any plugin requests with `workpane.capabilities.request` and should offer only while `workpane.capabilities.available` says something answers them, and which an agent asks only where its contract opens it to agents:

| Capability | Provider | Agents | Payload | Answer |
| --- | --- | --- | --- | --- |
| `workspace.page.open` | Browser | Yes | `{ url }`, an address the browser accepts | `{ tabId }`, once the page opened in a new active tab and the browser is on screen |
| `ssh.connections.list` | Remote SSH | No | An empty object | Public profile states, canonical connected roots and exact session identities without credential configuration. The detected `platform` is absent before the first successful connection |
| `ssh.workspace.open` | Remote SSH | No | `{ profile, session }` for the exact active connection | `{ workspace, profile, session, root, platform, name }` for its reserved consumer scope |
| `ssh.workspace.request` | Remote SSH | No | `{ workspace, method, parameters }` for a declared file or program method | The protocol value, or a structured failure |
| `ssh.workspace.close` | Remote SSH | No | `{ workspace }` belonging to the caller | An empty object after group preparation, accepted requests and cleanup settle |
| `workspace.folder.open` | Code Editor | No | `{ path }`, a readable folder | `{ workspaceId }`, once the folder is open, or brought to the front when it already was, and the editor is on screen |
| `workspace.file.open` | Code Editor | No | `{ path, root?, line?, column? }`, a readable file, an optional containing folder and positive positions counted from one | `{ workspaceId, path }`, with the editor and the requested file position on screen |
| `workspace.remote.open` | Code Editor | No | `{ profile, session }` for one connected SSH profile and its current connection identity | `{ workspaceId }`, with the remote editor on screen |
| `workspace.remote-path.open` | Code Editor | No | `{ profile, session, path, line?, column? }` for the exact connection, with positive positions counted from one | `{ workspaceId, path }`, opening the inspected remote file or folder in its remote workspace |
| `logs.entries.page` | Logs | Yes | `{ beforeSequence, limit }`, zero for the newest entries and at most a hundred | `{ entries }`, newest first, each with `sequence`, `timestampUtc`, `source`, `level`, `category`, `message` and `details` |
| `logs.entries.clear` | Logs | No | `{}` | `{}`, once every stored entry is gone, without the confirmation the reader sees |
| `workspace.folder.serve` | Web Server | No | `{ path }`, a readable local folder | An empty table at once, while the Web Server opens the form of the server for that folder, found by the canonical path of the folder so a link reaches the same server |
| `ai.task.start` | AI | No | `{ taskId }`, a task of the board | `{ taskId }`, once the task is queued to run, with its prompt sent again when it is an agent task |
| `ai.chat.complete` | AI | No | `{ messages, connection, maximumTokens, tools }`, where only `messages` is required | `{ content, reasoning, toolCalls, finishReason, usage, connection, model, adjustments }`, once the model answered |
| `terminal.workspace.snapshot` | Terminal | Yes | An empty table | `{ activeTerminalId, terminals }`, where each terminal carries `id`, `name` and the folder `cwd` its shell stands in, and the active one is empty when nothing has the focus |

The Web Server inspects a folder requested through `workspace.folder.serve` with SDK metadata and canonicalizes the complete original path. A trailing backslash in a POSIX folder name remains part of that name, and a failed inspection opens no server form.

The Remote SSH profile uses file fields for its local identity and OpenSSH configuration. Its root uses a folder field backed by a temporary authenticated connection, which is owned before connection preparation and closes when selection ends or its form is cancelled. Connection fields stay fixed while the selector uses their snapshot. An empty configured root selects the native home directory of the authenticated account. Browsing stays within the selected workspace root. AI task folder fields use the exact connected profile and session and disable browsing when unavailable. Local server roots, MCP executable and working directories use native path fields, and MCP root lists provide an Add a folder action.


The Code Editor selects remote move destinations through the existing workspace folder browser, within its open root, preserving cancellation without opening a local dialog.

The Code Editor routes workspace file reads, atomic saves, metadata, EditorConfig, tree operations, walks, search and file finding through one filesystem backend. A document keeps its accepted content revision separately from an external change observed while it is dirty, so observing a conflict cannot grant permission to overwrite that changed file.

Local metadata and save inspection use `workpane.files.stat`, which distinguishes a missing entry from an inspection failure. A failed metadata request preserves the open document, including when a remote request times out while the connection remains available. Only a successful inspection that finds no entry triggers removal. Loading reports failed inspection as a read failure, while a missing entry keeps its distinct message. Failed save inspection leaves the existing file untouched and creates no staging directory. Creating or relocating a file also refuses a destination whose inspection failed. Creating a file or folder requires the selected entry to exist before its prompt opens.

A language server initialization reply applies only to its exact process generation while the client is running. A reply received during stopping cannot make it ready or send document notifications. Each restarted server declares its own capabilities and synchronization state, with no retained declaration from the previous process.

The Code Editor reads its languages, the language server candidates of each one, the roles semantic tokens are painted with and its limits from `assets/languages.json`, and its color schemes from `assets/schemes.json`, where every scheme names every color of the editor including the `currentLine` fill and the `occurrence` tint, so a language, a server or a scheme is added by editing those files, and a language names the `constructs` it colors beyond its words, such as the headings and links of Markdown or the tags of HTML. It speaks the Language Server Protocol entirely in Lua over `workpane.process`, one server per open folder and language, sending the process identity of the product, naming to the reader a server that cannot be started and ending a server that does not answer its initialization in time, which is started again like one that ended by itself until the restart budget is spent. What a server writes to its error stream reaches the log at the debug level one line at a time with the language it serves, so an entry never stops in the middle of a line. The outline and the symbol search show each symbol with the icon of its kind in the tone of its family, with types in the warning tone, callables in the information tone and values in the success tone, and the client declares every kind the protocol numbers so a server never folds a newer kind into an older one. The editor tints the other uses of the symbol under the cursor from `textDocument/documentHighlight`, fades code a problem tags as unnecessary and strikes through code a problem or a semantic token tags as deprecated, opens a definition on a click with `mod`, and offers the edit actions before the navigation of the server on a secondary click. Every second the folder in front lists again the folders of its tree that are on screen and looks at its documents and at their EditorConfig files together, so files created, removed or renamed outside the product appear and disappear and reach the servers as watched files, while a folder behind it catches up once it comes to the front. A folder restored from the last session opens its documents and starts their servers only once the editor is first shown. Completion, hover text and navigation are asked of the text on screen, a document coming back in front is analyzed again only when it changed, and the cursors are stored when a document comes to the front, closes or the plugin stops. A file reaches its server under the identifier the protocol gives its extension, which a language names in `protocolIds`, such as `c`, `typescriptreact` and `javascriptreact`, and a problem with more related places than the editor takes shows the first of them with the last one counting the rest. Turning the servers on starts one for every language without one, and a search for servers restarts only a server whose program changed. Saving, closing and reading a file again first flush the editor, so the last keystrokes are never lost, a save writes beside the file and moves over it with `workpane.files.replace`, keeping its permissions and writing again a file removed outside the product. A failed replacement reports failure, keeps the document unsaved and restores the previous encoding choice. Workspace shutdown waits within its grace for saves already accepted and prevents a pending editor flush from starting a new save after its document closed, and a file read again keeps the place of the reader and reaches its language server at once.

The AI plugin reads its providers, their protocols, parameters, endpoints and command line agents and its limits from `assets/providers.json`, and the window, answer budget, traits and prices of every model from `assets/models.json`, so a provider or a model is added by editing those files. It speaks the Anthropic and OpenAI-compatible protocols over an owned `http.client.exchange`, the Model Context Protocol over `workpane.process` and streamable HTTP, and JSON through a codec of its own that keeps empty lists and null. A task may carry a schedule that runs it once, on an interval or on a cron expression, a cron expression keeps the time zone of the system it was written in and finds its occurrences on that wall clock, a start by hand keeps that schedule, and a due schedule whose start is refused, such as one whose agent was removed, still moves to its next occurrence and is reported once. Its agents reach the native tools that read, write and search the working directory of their task, run its commands, read the web, generate media, read the skills and agent plugins, read the task and the other tasks of its board, list the plugins of Workpane and ask a capability another plugin opens to agents, besides the tools, resources and prompts of the configured servers. A call to a server reaches the client that server has when the call runs, so a server restarted during a run still answers, and a request a server leaves unanswered past its deadline, or that a stopped task no longer waits for, fails with `ai_mcp_timeout` or `ai_mcp_cancelled` and is withdrawn at the server. A server that does not finish its initialization within the start time of the catalog is stopped, a server counts as ready once its tools arrived, page after page within a bound, the roots a server may read are absolute folders that change only by starting it again, and a request of a server that fails is answered with an internal error. A command line agent whose provider sets `promptInput` receives the conversation on its standard input, which no limit of a command line bounds, and the others receive it as the argument `{prompt}` names. A run that fails tells the reader why in their language and records a safe structured code without copying the provider or program transcript into its diagnostics. A search of files that must contain a text runs on a worker through `workpane.files.search`. The chat of an agent task is written at the size its zoom buttons keep in the settings of the plugin, shows the text of a turn once while its tools run, draws a summary that replaced the earlier conversation as a note of the conversation rather than as a message of the reader, and keeps a message it could not record in the composer. The settings of the plugin keep one key per value, each edited through the control of its store, and the working folder of a task must be an existing folder.

An MCP client stopped while looking up its executable discards the late result without launching a process or initializing the server. An HTTP session keeps the authorization accepted when its session identity arrived and uses it when deleting that session. Closing or renewing clears the client reference, while a deletion already accepted keeps its request headers until delivery. A session whose initialization response arrives after cancellation is deleted through the same owned request without making the client ready. This lets shutdown release the session even when its credential source has changed.

Each terminal of the Terminal plugin keeps the shell it was started with and a history file of its own under `terminal/history` in the data directory, so the commands of one terminal never reach the history of another, and the file of a terminal the reader closed is removed the next time the workspace loads, shows a notification its program sends under the name of the terminal, shows a file address in the file manager of the system, offers Restart under a terminal whose shell could not start, and shows its shell beside a renamed terminal, a detail the header gives up first as it narrows before its focus and menu buttons. Closing the last tab leaves the Terminal without tabs, even after a restart, and its empty view opens a new workspace with one terminal, which the new terminal shortcut does too, while the snapshot then names no active terminal. Focus mode zooms the focused terminal, so zooming one focuses it and another terminal taking the focus, such as a new one, ends the zoom. The Terminal plugin also publishes `terminal.workspace.changed` with an empty payload only when its terminals, their names, their folders or the active one changed, so a subscriber asks for a new snapshot, and `terminal.session.closed` with `terminalId` when a terminal ends.

### Conversations with a model

The capability `ai.chat.complete` answers a conversation the way `litellm.completion` does. The caller writes one list of messages, and the AI plugin checks it whole, fits it to the model of the connection and writes it in the protocol of that provider. Each message has a `role` of `system`, `user`, `assistant` or `tool` and a `content` that is a text or a list of typed parts:

| Part | Fields | Carried by |
| --- | --- | --- |
| `text` | `text` | Every role |
| `image` | `mediaType` of PNG, JPEG, GIF or WebP with `data`, the bytes, or `url`, a web address, and an optional `detail` of `low`, `high` or `auto` | User and tool messages |
| `document` | `mediaType` of `application/pdf` or a text type such as `text/plain`, `text/markdown` or `application/json`, `name` and `data` | User messages |
| `audio` | `format` of `wav` or `mp3` and `data` | User messages |
| `reasoning` | `text`, and the `signature` or the `redacted` data a provider proved it with, one part for each block the model wrote | Assistant messages |

An assistant message may call tools with `toolCalls`, each with an `id` unique in its turn, a `name` and `arguments` as a table, and every call is answered before the next message by a `tool` message naming it in `toolCallId`, with `failed` set when the tool failed. A conversation that breaks a rule is refused with the code of that rule and the place in `detail`, such as `ai_message_part_invalid` at `messages[2].content[1].mediaType`, so nothing reaches a provider that would refuse it.

The plugin fits the conversation to the model from the traits `assets/models.json` gives it: `vision`, `pdf`, `audio`, `system-prompt` and `function-calling`. An image, a PDF or an audio the model cannot read becomes a note that says so, a text document becomes its text headed by its name, instructions reach a model without a system role as a user message, and earlier tool calls reach a model that calls no tool as text. The answer lists each kind of change in `adjustments`, and an agent writes it once in the log of its run. The Anthropic protocol receives the instructions apart, turns of one role joined, images, documents, every signed or redacted block of reasoning of an earlier turn in its order and results with their images as blocks, while the OpenAI protocol receives parts in its own shape, with the images of tool results in a user turn after them. Both report `finishReason` as `stop`, `length`, `tool_calls` or `content_filter`.

The connection is named by its key, such as `openai/gpt-5`, or is the default one, and a command-line connection is refused with `ai_completion_command_line`, since a command-line agent runs a task of its own in a folder with the permissions of the reader rather than answering a completion, which is also why a server asking to sample is never answered by one, `maximumTokens` bounds the answer, and `tools` declares tools with a `name`, a `description` and the object schema of their `parameters`, which the caller runs itself from the `toolCalls` of the answer. The agents of the board are answered by the same layer, so a file attached to a message in a conversation travels the same way, and a call a stopped run left unanswered is answered as interrupted the next time the task runs.

### What an agent knows and reaches

The system prompt of an agent opens with the instructions of the reader and of the repository, before anything the prompt of the agent says. The reader gives the first of `~/.agents/AGENTS.md`, `~/.codex/AGENTS.md`, `~/.config/opencode/AGENTS.md`, `~/.config/amp/AGENTS.md`, `~/.claude/AGENTS.md` and `~/.gemini/GEMINI.md` that exists. The repository is the working directory or the nearest folder above it that holds `.git`, a working directory outside any repository is its own root, and every folder from its root down to the working directory gives the first of `AGENTS.md`, `AGENT.md`, `GEMINI.md`, `.github/copilot-instructions.md`, `.cursorrules` and `.windsurfrules` it holds, with `AGENTS.md` taking precedence in each folder. A document imports another one written as `@path` outside code, relative to the importing document, as an absolute path or with `~/` for the home folder, up to four documents deep, an import joins only when it stays inside the repository, or inside the home folder for the instructions of the reader, so a cloned repository never brings a file of the reader into the prompt, every document joins once, and the instructions keep a bound of bytes past which a document is cut and the cut is said. The instructions of a run are built once and again only when the tools it offers change, so the provider keeps their prefix cached, and the time and the servers they name are the ones of the moment they were built.

A skill is a folder holding `SKILL.md` whose front matter carries `name`, `description` and, as the Agent Skills standard defines them, `license`, `compatibility`, `allowed-tools` and `metadata`, read as YAML with quoted values, folded and literal blocks, lists and maps. Skills come from `.agents/skills`, `.claude/skills`, `.codex/skills`, `.gemini/skills`, `.opencode/skills`, `.opencode/skill`, `.cursor/skills`, `.github/skills`, `.kimi/skills`, `.windsurf/skills`, `.devin/skills`, `.goose/skills`, `.cline/skills` and `.clinerules/skills` of the working directory and of the root of its repository, and from `.agents/skills`, `.config/agents/skills`, `.claude/skills`, `.codex/skills`, `.gemini/skills`, `.config/opencode/skills`, `.config/opencode/skill`, `.cursor/skills`, `.copilot/skills`, `.kimi/skills`, `.codeium/windsurf/skills`, `.config/devin/skills`, `.config/goose/skills` and `.cline/skills` of the home folder, a link counting as the folder it reaches, and the first skill of a name wins. A skill without a description is left out with a warning, and the catalog of the system prompt names each skill with its description, flattened to one bounded line, and the path of its instructions, so an agent without tools reads it there, while a catalog that passes its bound says that the tools reach the rest.

An agent plugin is a folder with a manifest of Claude Code in `.claude-plugin/plugin.json`, of Codex in `.codex-plugin/plugin.json`, of Cursor in `.cursor-plugin/plugin.json`, of the Agent Plugins standard in `.plugin/plugin.json` or `plugin.json` or of a Gemini CLI extension in `gemini-extension.json`, found in `.agents/plugins` and `.gemini/extensions` of the project and of the home folder and in the caches where Claude Code and Codex install the plugins of their marketplaces. A plugin contributes its skills under its own name, such as `kit:format`, and the agent lists its commands, agents, servers, hooks and context file with `list_agent_plugins` and reads any of its files with `read_agent_plugin_file`.

Beside the files, commands, web and media tools, an agent reads its task with `describe_task`, lists the tasks of its board with `list_tasks`, reads another task with its latest runs with `read_task`, searches the text of the working directory with `search_text`, lists the plugins of Workpane with `list_workpane_plugins` and the capabilities they provide with `list_workpane_capabilities`, and asks one with `request_workpane_capability`, both of which reach only the capabilities whose contract opens them to agents, so no run erases what the reader keeps, serves or opens a folder outside its own or starts another run.

The prompt templates live in `assets/templates`: `catalog.json` names every template and the Markdown sections it joins in order, the sections every agent shares are written once, such as the principles, the way of planning, executing, verifying and reviewing, the formatting of code, security, interface, tests, version control, documentation, images and the report, and each platform or area adds its own section. Their text is written in English for the model, while their names and descriptions come from the catalog of the plugin, and inserting a template over a prompt the reader wrote asks before replacing it. An agent can be saved and run with an empty system prompt, which produces no empty system message in its request.

The chat shows a submitted message and its pending phase while recording it, keeps the draft until recording succeeds and prevents another submission during that write. Stopping the task during that write keeps the recorded message and cancels the pending execution, so the reply from storage cannot start a stopped run. Its Markdown supports selection, copying, web links and file paths, resolving relative paths against the task directory. File and folder links use the Code Editor capabilities, which the AI plugin watches so an unavailable editor is reported cleanly. A file request validates its optional root and brings its requested line and column into view.

Failures shown on cards, in notifications and in logs use a short translated reason and structured codes instead of a command transcript or a conversation. The diagnostic log keeps bounded structured failure codes, including the underlying code of a refused program start, and bounded numeric fields for HTTP status, process exit, input sizes and command deadlines. Known provider refusals receive translated reasons, and raw HTTP error bodies and stream error messages never enter a public failure. Command line agents stream only their output stream into the chat, with diagnostics kept out of assistant messages and a bound applied to both streams. Execution logs describe tool names, response sizes and phases without copying prompts, tool arguments or message bodies. Double clicking an idle task in To Do edits it, while other columns open details. The execution log table fills its tab. Markdown images in the conversation resolve relative paths against the working folder of their task and load through the SDK without blocking the interface.

The Browser uses the platform modifier for closing tabs, refreshing pages and focusing the address field, with `mod+w`, `mod+r` and `mod+l`, and also refreshes with F5. Its previous and next navigation actions use Command with the brackets on macOS and Alt with the left and right arrows elsewhere, leaving the word movement keys to editing on macOS. Opening a new tab focuses its address field. These actions remain available while the native page has the keyboard, while copy, paste and the other text editing actions stay in the native engine.

## Checking a plugin

The audits of `python3 make.py audit` refuse a plugin without its two files, a catalog missing a key in one language, a key outside the grammar, a key whose languages declare different arguments and a key nothing in the plugin reaches. The product suite boots every bundled plugin headless, builds their views and fails on any error written to the log.

Bounded file reading validates the path before the runtime serializes it, accepts a maximum from zero through 64 MiB and preserves every binary byte through the JSON bridge. The native reader applies the bound while reading rather than trusting a previous size check. It releases its file on completion, truncation, cancellation or read failure and never returns a partial result after a read failure. The editor refuses an incomplete document. Agent resource discovery keeps the bounded prefix with its truncation flag, and image tooling refuses an incomplete image.

Process input and writes preserve binary strings through an encoded internal JSON transport. The SDK and native host bound each input to 64 MiB, refuse malformed encoded data and validate launch and executable search text before serialization. An absent initial input leaves standard input open. An explicitly empty initial input closes it. Filesystem requests also validate their original path text before serialization so malformed UTF-8 cannot become a different path.

Agent tools check declared argument types before calling native implementations or MCP servers. An `integer` argument must be a finite whole numeric value, including an integral floating representation such as `2.0`. A fractional argument produces a tool failure naming that argument and never reaches its implementation.

### Remote task consumers and image readers

The AI task form stores `hostKind`, `hostProfile` and `hostPlatform` alongside its working directory. Every remote run acquires a fresh workspace and uses its filesystem, process adapter and read-only account context. File tool edits retain the accepted remote revision. A disconnected profile refuses execution, and a task remains busy while its accepted resource group closes. Tool cancellation identifies the call object, avoiding collisions between provider identifiers from different tasks.

The function `workpane.ui.loadMarkdownImage(event, backend?)` returns a future of the `load-image` reply for a Markdown image request. The optional backend declares `platform` and `readBounded`, so file addresses and relative paths use that platform and reader. Binary file images must be complete within the image bound and carry a supported PNG, JPEG or GIF signature. The reply carries an encoded data address for a supplied reader and a native path otherwise. HTTP and data addresses use the same bounded loader as the default image handler. A plugin installing its own `image-request` handler must answer the node with `load-image`, including the original `source` and `request`, or a reply with `failed = true`.

## Plugins in a derived application

An application assembled with the [Workpane foundation](foundation.md) uses the same manifest, SDK, lifecycle and component contracts. Its own plugin folders are selected through `workpane_add_application`, and no bundled Workpane feature is included by default. The fields `workpane.app.name`, `workpane.app.organization`, `workpane.app.identifier`, `workpane.app.version` and `workpane.app.buildNumber` describe that application. The fields `workpane.app.engineVersion` and `workpane.app.engineBuildNumber` describe the Workpane base. Native credentials are isolated by both the application and plugin identifiers.
