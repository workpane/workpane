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
    sdk = 1,
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

## The API

### Plugin and application

| Name | Meaning |
| --- | --- |
| `workpane.plugin.id`, `workpane.plugin.directory` | The identity and the folder of this plugin |
| `workpane.app.version`, `sdk`, `debug`, `platform`, `architecture` | What the running product is and the level of its SDK |
| `workpane.app.processId` | The process identity of the product, which a language server receives so it ends with the product |
| `workpane.app.dataDirectory` | The data directory of the product, where a plugin keeps files such as the shell history of its terminals |
| `workpane.app.languages`, `themes`, `icons`, `colors` | What the product offers, where the colors are the roles of the palette described in the component reference |
| `workpane.app.language()`, `workpane.app.theme()` | The language and theme selected now |
| `workpane.app.watch(handler)` | Receives `{ language, theme }` after each change the reader makes and answers the function that stops it, and anything but a function is refused with `reader_handler_invalid` |
| `workpane.app.quit()` | Asks the reader to confirm quitting |
| `workpane.app.plugins()` | Every plugin the product discovered with its `id`, `titleKey`, `descriptionKey`, whether it is `bundled` and its `state`, as copies of plain data |

### Shell

| Name | Meaning |
| --- | --- |
| `workpane.shell.navigate(item)` | Shows another destination of the same plugin |
| `workpane.shell.resizeBand(item, height)` | Changes the height of a band of the plugin to zero, which hides it, or to 16 to 320 points, and the band keeps the new height until the plugin stops |

A band this plugin does not declare is refused with `shell_band_unknown` and a height out of range with `shell_band_height_invalid`.

### Interface

The builders of `workpane.ui` create the components listed in [Components](components.md), and `workpane.ui.isNode(value)` answers whether a value is a node one of them built.

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
| Any other `string` | `textField` and `secretField`, which write when the reader leaves them or presses Enter, `textArea`, `combo` with the `options` its properties give |

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
| `workpane.database.invalid(detail)` | Raises `database_rows_invalid` for stored rows that break the rules of the plugin, which refuses its start and names the plugin and the database file to the reader |

The `migrations` of the definition are applied before every start of the plugin, each list after the stored version once and atomically, so its `start` already finds its tables as its code expects them, and a migration that fails refuses the plugin with its code. Since the definition is read when the plugin is discovered, the product knows the newest schema every installed plugin reads even when it never ran here or is turned off, which is what an imported configuration is proven against.

Every call answers a future and runs on the database thread. A plugin creates, changes and drops its own tables, indexes, views and triggers only in its migrations, where a view and a trigger carry the prefix of the plugin, a trigger watches only a table of its plugin, a column can be added, renamed or dropped and a foreign key refers only to a table of the plugin, or the migration is refused with `database_foreign_key_foreign`. A name the plugin gives, even a common table expression of a query, starts with its prefix in lowercase, since a name written in other letters is never the plugin's own. A statement reaching a table of another plugin or of the core, the schema itself, a pragma, an attached database or a transaction command is refused, and renaming a table is refused because it could leave the namespace of the plugin. A statement runs for at most ten seconds and a migration for at most two minutes, and one that runs longer, or is still running as the product closes, is interrupted with `database_interrupted`, so no plugin holds the database of the others. A binding is a finite number, a text, a boolean or `workpane.database.null`, and a number that is not finite is refused with `database_binding_invalid`. A migration list only ever grows, since a stored version newer than the list is refused. At every start the tables, indexes, views and triggers the plugin keeps are compared both ways with the ones its migrations create in a database in memory, ignoring spacing and quotes, so an object the migrations no longer create refuses the plugin as surely as one they create differently. A difference refuses the plugin with `database_schema_mismatch` naming the object, which the reader is told as stored data that could not be read, with the place of the file, and the reader can then erase the data of the plugin from the Plugins section.

### Events and capabilities

| Name | Meaning |
| --- | --- |
| `workpane.events.publish(topic, payload)` | Delivers a payload to every other plugin subscribed to the topic, and the topic starts with the plugin identifier |
| `workpane.events.subscribe(topic, handler)` | Receives the payload and the identifier of the sender |
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

Topics and capability names follow the grammar of translation keys. A handler that is not a function is refused when it is given, with `event_handler_invalid` or `capability_handler_invalid`. A payload and the value a provider answers cross between plugins as copies of plain data, so every subscriber receives a copy of its own, and a function, a userdata, a table with a metatable other than the mark of the `json` module or a cycle among them is refused with `event_payload_invalid` or `capability_value_invalid`.

### Dialogs and notifications

| Name | Answer |
| --- | --- |
| `workpane.dialogs.confirm({ title, message, detail, confirmText, cancelText, destructive })` | True only when the reader confirmed |
| `workpane.dialogs.alert({ title, message, detail, confirmText })` | Nothing, once closed |
| `workpane.dialogs.prompt({ title, message, value, placeholder, confirmText, cancelText })` | The text, or nil when cancelled |
| `workpane.dialogs.custom({ title, message, content, buttons, width, onButton })` | The identifier of the button that closed it |
| `workpane.dialogs.openFile({ title, initial, filters, multiple })` | The chosen paths, empty when cancelled |
| `workpane.dialogs.selectFolder({ title, initial })` | The chosen folder, or nil |
| `workpane.dialogs.saveFile({ title, initial, filters })` | The chosen path, or nil |
| `workpane.dialogs.message({ title, message, kind, buttons })` | The button the reader pressed |

Every dialog answers a future. A filter is `{ name, patterns }`, a message kind is `information`, `warning`, `error` or `question`, and message buttons are `ok`, `ok-cancel`, `yes-no` or `yes-no-cancel`. The title, message, detail and button texts of a dialog are strings written with `workpane.i18n.translate`, because a dialog shows them only while it is open. A custom dialog shows its `content` tree, and its components keep their values after it closes, so the plugin reads them from the nodes. Its buttons sit under the content and carry `id`, `text` and the optional `variant`, `icon`, `enabled` and `closes`. A button with `closes = false` keeps the dialog open and calls `onButton(id, dialog)` in a task of the plugin, which checks its form and calls `dialog:close(id)` once it is valid, and `dialog:setButtons(buttons)` replaces the buttons while the dialog is open, so they follow the state of what it shows. Escape and `mod+w` close any dialog with `cancel`, after Escape first closes a list or a menu opened inside it.

A dialog takes the height its content needs when it opens, bounded by the window, and keeps it: its title and its buttons stay in place and the content scrolls between them clear of the scroll bar, so a problem that appears or a page that switches never resizes it and the bar never covers a control. A form lays out each field with `ui.formField`, which writes the label above its control followed by a colon, and a checkbox keeps its own text without a label beside it. A problem the reader must fix before a form can be saved, such as a field that is missing, is shown with `ui.alert` inside the form rather than with a colored label, so every problem of the product looks the same. An alert takes a `tone` for a warning, a success or a note, and text a plugin writes over a colored fill uses the ink of that fill, such as `on-warning` over `warning`.

A dialog opened while another one is on screen is drawn above it and answered first, so a form can ask for a confirmation without closing. The dialogs of a plugin that stops close as cancelled, so a coroutine waiting on one of them ends instead of waiting forever.

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
| `workpane.system.revealPath(path)` | Shows a file or folder in the file manager and answers a future, refusing a path that is not absolute with `system_path_invalid` |
| `workpane.system.copy(text)`, `workpane.system.paste()` | Writes and reads the text of the system clipboard |
| `workpane.system.home()` | The home directory of the reader, where a new terminal or a folder picker starts |
| `workpane.system.shell()` | The default shell a terminal component starts, as `{ name, path }`, whose name such as `zsh` or `pwsh` names a new terminal |
| `workpane.system.information()` | A future of a snapshot of the machine, collected on a worker in about a fifth of a second |
| `workpane.system.monospaceFonts()` | A future of the names of the monospaced families installed on the machine in alphabetical order, listed once on a worker and offered as the `fontFamily` of a code editor or a terminal |
| `workpane.time.now()` | The current moment as a stored UTC timestamp |
| `workpane.time.localPresentation(timestamp)` | The moment written in the local zone of the reader |
| `workpane.time.zone()` | The time zone the system is set to, named as the time zone database names it, such as `Europe/Lisbon` |
| `workpane.time.offset(zone, seconds)` | The offset in seconds from UTC that a zone has at an instant given in seconds since the epoch, raising `time_zone_unknown` for a zone the system does not know |

Entries reach the subscribers of the log once per frame, each in a protected task. A handler that is not a function is refused with `log_handler_invalid`, a subscriber that fails is removed after its first failure, which is written once, and a subscriber that writes to the log while it reads an entry is refused with `log_write_in_delivery`, so no subscriber feeds itself. A path given to `revealPath` must be absolute and fails with `system_path_invalid` otherwise, and every path the host answers, such as the home directory, is written with forward slashes on every platform, where a network share of Windows starts with two of them.

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

A server is started with a table whose `onRequest` is a function when given, and anything else is refused with `http_options_invalid`. A server answers `GET` requests for the files under its root, with the index of a folder for a folder, and refuses everything else. The host is a numeric IPv4 or IPv6 address and the port is from 1 to 65535, where any other port is refused with `json_field_range`. A root that is not a readable folder fails with `http_root_invalid`, a host with `http_host_invalid` and a port another program holds with `http_bind_failed`.

The `onRequest` handler receives the requests of a moment together, oldest first, each with `timestamp`, `method`, `path`, `status`, `durationMs`, `responseBytes` and `remoteAddress`. The servers of a plugin stop when the plugin stops or is withdrawn.

### Reading folders

| Name | Meaning |
| --- | --- |
| `workpane.files.list(path)` | A future of the entries of a folder, each with `name`, `kind` of `directory`, `file`, `symlink` or `other`, and the `size` of a file |
| `workpane.files.walk(root, { maximum, skip })` | A future of `{ paths, complete }`, the files under the folder relative to it with forward slashes, hidden ones included, up to `maximum` |
| `workpane.files.search(root, { text, maximumMatches, maximumFileBytes, skip })` | A future of `{ matches, complete }`, each match with `path`, `line` from one and the trimmed `text` of the line |
| `workpane.files.access(path)` | A future answering `readable` and `writable` for the account running the product, for a folder as for a file, or failing with `files_path_missing` |
| `workpane.files.canonical(path)` | A future of the path with every link and dot resolved |
| `workpane.files.replace(source, destination)` | A future settled once the file written at `source` was moved over `destination` with the permissions of the file it replaces, creating the destination when it is gone |
| `workpane.files.absolute(path)` | Whether a path is absolute on the platform the product runs on: from the root on macOS and Linux, from a drive or a network share such as `//server/share` on Windows |
| `workpane.files.uri(path)` | The `file:` address of an absolute path, with every byte outside the unreserved set encoded, the slash a Windows drive needs and the server of a network share as its host, refusing a relative path with `files_path_invalid` |
| `workpane.files.path(uri)` | The path a `file:` address names, or nil for any other address, where a Windows drive loses the slash before it and another server is a network share on Windows |

Each function that answers a future runs on a worker, and every path is absolute. The three that read and write paths answer at once, and a plugin judges and converts paths only through them, so every plugin follows the same rule the host checks again. A walk and a search never follow a linked folder and leave out the folders named in `skip`, such as `.git`, and leave out an entry they cannot read, and `complete` is false when the bound stopped them or the product quit before they finished. A search compares ASCII letters without regard to case, reads only files no larger than `maximumFileBytes` that hold UTF-8 text without a NUL byte, and cuts the text of a match at 400 characters. A folder that cannot be read fails with `files_directory_unavailable`, a relative path with `files_path_invalid`, a path that does not exist with `files_path_missing`, more than 64 folders in `skip` with `files_skip_too_long` and a search text longer than 1000 bytes with `files_search_text_too_long`. The `fs` module of Varn remains the way to read, write, rename and remove files.

### Running programs

| Name | Meaning |
| --- | --- |
| `workpane.process.start({ program, arguments, directory, variables, cleared, input, onOutput, onExit })` | Starts a program at once and answers its identifier, and a program given `input` reads that text and then the end of its input, so an empty one ends its input from the start and a program that would wait for input finishes instead, while one given none keeps its input open for `write` |
| `workpane.process.write(process, text)` | Sends text to the standard input of a program |
| `workpane.process.stop(process)` | Asks a program to end, and ends it with everything it started two seconds later |
| `workpane.process.find(name, directories)` | A future of the absolute path of an executable found on the search path or in the added directories, or nil |

A program is started with a table whose `onOutput` and `onExit` are functions when given, and anything else is refused with `process_options_invalid`. A program is named by an absolute path and starts without a shell, in an absolute directory, with the arguments as they are given, the inherited environment without the `cleared` names and with the `variables` added. A program or a directory that is not absolute, too many arguments, a variable without a name or a cleared name that is not one is refused with `process_launch_invalid`, a path that is not an executable fails with `process_program_missing`, a missing directory with `process_directory_missing`, and text a program can no longer take with `process_input_refused`. On Windows a batch file runs through the command prompt.

The `onOutput` handler receives what arrived since the last turn of the loop as a list of chunks in order, each with `stream`, which is `output` or `error`, and `text`, which is UTF-8 with a character split between two reads kept whole and anything else replaced by U+FFFD. A handler that reads a protocol from them keeps its buffer without waiting, so the chunks stay in order. The `onExit` handler receives the code after the last output and whether the program crashed, where a program ended by a signal on POSIX reports 128 plus the signal and one ended by an exception on Windows reports its status. The programs of a plugin end when the plugin stops or is withdrawn.

## Capabilities of the bundled plugins

The bundled plugins answer these capabilities, which any plugin requests with `workpane.capabilities.request` and should offer only while `workpane.capabilities.available` says something answers them, and which an agent asks only where its contract opens it to agents:

| Capability | Provider | Agents | Payload | Answer |
| --- | --- | --- | --- | --- |
| `workspace.page.open` | Browser | Yes | `{ url }`, an address the browser accepts | `{ tabId }`, once the page opened in a new active tab and the browser is on screen |
| `workspace.folder.open` | Code Editor | No | `{ path }`, a readable folder | `{ workspaceId }`, once the folder is open, or brought to the front when it already was, and the editor is on screen |
| `logs.entries.page` | Logs | Yes | `{ beforeSequence, limit }`, zero for the newest entries and at most a hundred | `{ entries }`, newest first, each with `sequence`, `timestampUtc`, `source`, `level`, `category`, `message` and `details` |
| `logs.entries.clear` | Logs | No | `{}` | `{}`, once every stored entry is gone, without the confirmation the reader sees |
| `workspace.folder.serve` | Web Server | No | `{ path }`, a readable folder | An empty table at once, while the Web Server opens the form of the server for that folder, found by the canonical path of the folder so a link reaches the same server |
| `ai.task.start` | AI | No | `{ taskId }`, a task of the board | `{ taskId }`, once the task is queued to run, with its prompt sent again when it is an agent task |
| `ai.chat.complete` | AI | No | `{ messages, connection, maximumTokens, tools }`, where only `messages` is required | `{ content, reasoning, toolCalls, finishReason, usage, connection, model, adjustments }`, once the model answered |
| `terminal.workspace.snapshot` | Terminal | Yes | An empty table | `{ activeTerminalId, terminals }`, where each terminal carries `id`, `name` and the folder `cwd` its shell stands in, and the active one is empty when nothing has the focus |

The Code Editor reads its languages, the language server candidates of each one, the roles semantic tokens are painted with and its limits from `assets/languages.json`, and its color schemes from `assets/schemes.json`, where every scheme names every color of the editor including the `currentLine` fill and the `occurrence` tint, so a language, a server or a scheme is added by editing those files, and a language names the `constructs` it colors beyond its words, such as the headings and links of Markdown or the tags of HTML. It speaks the Language Server Protocol entirely in Lua over `workpane.process`, one server per open folder and language, sending the process identity of the product, naming to the reader a server that cannot be started and ending a server that does not answer its initialization in time, which is started again like one that ended by itself until the restart budget is spent. What a server writes to its error stream reaches the log at the debug level one line at a time with the language it serves, so an entry never stops in the middle of a line. The outline and the symbol search show each symbol with the icon of its kind in the tone of its family, with types in the warning tone, callables in the information tone and values in the success tone, and the client declares every kind the protocol numbers so a server never folds a newer kind into an older one. The editor tints the other uses of the symbol under the cursor from `textDocument/documentHighlight`, fades code a problem tags as unnecessary and strikes through code a problem or a semantic token tags as deprecated, opens a definition on a click with `mod`, and offers the edit actions before the navigation of the server on a secondary click. Every second the folder in front lists again the folders of its tree that are on screen and looks at its documents and at their EditorConfig files together, so files created, removed or renamed outside the product appear and disappear and reach the servers as watched files, while a folder behind it catches up once it comes to the front. A folder restored from the last session opens its documents and starts their servers only once the editor is first shown. Completion, hover text and navigation are asked of the text on screen, a document coming back in front is analyzed again only when it changed, and the cursors are stored when a document comes to the front, closes or the plugin stops. A file reaches its server under the identifier the protocol gives its extension, which a language names in `protocolIds`, such as `c`, `typescriptreact` and `javascriptreact`, and a problem with more related places than the editor takes shows the first of them with the last one counting the rest. Turning the servers on starts one for every language without one, and a search for servers restarts only a server whose program changed. Saving, closing and reading a file again first flush the editor, so the last keystrokes are never lost, a save writes beside the file and moves over it with `workpane.files.replace`, keeping its permissions and writing again a file removed outside the product, and a file read again keeps the place of the reader and reaches its language server at once.

The AI plugin reads its providers, their protocols, parameters, endpoints and command line agents and its limits from `assets/providers.json`, and the window, answer budget, traits and prices of every model from `assets/models.json`, so a provider or a model is added by editing those files. It speaks the Anthropic and OpenAI-compatible protocols over `http.client.stream`, the Model Context Protocol over `workpane.process` and streamable HTTP, and JSON through a codec of its own that keeps empty lists and null. A task may carry a schedule that runs it once, on an interval or on a cron expression, a cron expression keeps the time zone of the system it was written in and finds its occurrences on that wall clock, a start by hand keeps that schedule, and a due schedule whose start is refused, such as one whose agent was removed, still moves to its next occurrence and is reported once. Its agents reach the native tools that read, write and search the working directory of their task, run its commands, read the web, generate media, read the skills and agent plugins, read the task and the other tasks of its board, list the plugins of Workpane and ask a capability another plugin opens to agents, besides the tools, resources and prompts of the configured servers. A call to a server reaches the client that server has when the call runs, so a server restarted during a run still answers, and a request a server leaves unanswered past its deadline, or that a stopped task no longer waits for, fails with `ai_mcp_timeout` or `ai_mcp_cancelled` and is withdrawn at the server. A server that does not finish its initialization within the start time of the catalog is stopped, a server counts as ready once its tools arrived, page after page within a bound, the roots a server may read are absolute folders that change only by starting it again, and a request of a server that fails is answered with an internal error. A command line agent whose provider sets `promptInput` receives the conversation on its standard input, which no limit of a command line bounds, and the others receive it as the argument `{prompt}` names. A run that fails tells the reader why in their language, with the words of the provider or of the program inside, and keeps the English message of the failure for the log. A search of files that must contain a text runs on a worker through `workpane.files.search`. The chat of an agent task is written at the size its zoom buttons keep in the settings of the plugin, shows the text of a turn once while its tools run, draws a summary that replaced the earlier conversation as a note of the conversation rather than as a message of the reader, and keeps a message it could not record in the composer. The settings of the plugin keep one key per value, each edited through the control of its store, and the working folder of a task must be an existing folder.

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

The system prompt of an agent opens with the instructions of the reader and of the repository, before anything the prompt of the agent says. The reader gives the first of `~/.agents/AGENTS.md`, `~/.codex/AGENTS.md`, `~/.config/opencode/AGENTS.md`, `~/.config/amp/AGENTS.md`, `~/.claude/CLAUDE.md` and `~/.gemini/GEMINI.md` that exists. The repository is the working directory or the nearest folder above it that holds `.git`, a working directory outside any repository is its own root, and every folder from its root down to the working directory gives the first of `AGENTS.md`, `CLAUDE.md`, `AGENT.md`, `GEMINI.md`, `.github/copilot-instructions.md`, `.cursorrules` and `.windsurfrules` it holds, so a repository with `AGENTS.md` is read by it and `CLAUDE.md` only where no `AGENTS.md` exists. A document imports another one written as `@path` outside code, relative to the importing document, as an absolute path or with `~/` for the home folder, up to four documents deep, an import joins only when it stays inside the repository, or inside the home folder for the instructions of the reader, so a cloned repository never brings a file of the reader into the prompt, every document joins once, and the instructions keep a bound of bytes past which a document is cut and the cut is said. The instructions of a run are built once and again only when the tools it offers change, so the provider keeps their prefix cached, and the time and the servers they name are the ones of the moment they were built.

A skill is a folder holding `SKILL.md` whose front matter carries `name`, `description` and, as the Agent Skills standard defines them, `license`, `compatibility`, `allowed-tools` and `metadata`, read as YAML with quoted values, folded and literal blocks, lists and maps. Skills come from `.agents/skills`, `.claude/skills`, `.codex/skills`, `.gemini/skills`, `.opencode/skills`, `.opencode/skill`, `.cursor/skills`, `.github/skills`, `.kimi/skills`, `.windsurf/skills`, `.devin/skills`, `.goose/skills`, `.cline/skills` and `.clinerules/skills` of the working directory and of the root of its repository, and from `.agents/skills`, `.config/agents/skills`, `.claude/skills`, `.codex/skills`, `.gemini/skills`, `.config/opencode/skills`, `.config/opencode/skill`, `.cursor/skills`, `.copilot/skills`, `.kimi/skills`, `.codeium/windsurf/skills`, `.config/devin/skills`, `.config/goose/skills` and `.cline/skills` of the home folder, a link counting as the folder it reaches, and the first skill of a name wins. A skill without a description is left out with a warning, and the catalog of the system prompt names each skill with its description, flattened to one bounded line, and the path of its instructions, so an agent without tools reads it there, while a catalog that passes its bound says that the tools reach the rest.

An agent plugin is a folder with a manifest of Claude Code in `.claude-plugin/plugin.json`, of Codex in `.codex-plugin/plugin.json`, of Cursor in `.cursor-plugin/plugin.json`, of the Agent Plugins standard in `.plugin/plugin.json` or `plugin.json` or of a Gemini CLI extension in `gemini-extension.json`, found in `.agents/plugins` and `.gemini/extensions` of the project and of the home folder and in the caches where Claude Code and Codex install the plugins of their marketplaces. A plugin contributes its skills under its own name, such as `kit:format`, and the agent lists its commands, agents, servers, hooks and context file with `list_agent_plugins` and reads any of its files with `read_agent_plugin_file`.

Beside the files, commands, web and media tools, an agent reads its task with `describe_task`, lists the tasks of its board with `list_tasks`, reads another task with its latest runs with `read_task`, searches the text of the working directory with `search_text`, lists the plugins of Workpane with `list_workpane_plugins` and the capabilities they provide with `list_workpane_capabilities`, and asks one with `request_workpane_capability`, both of which reach only the capabilities whose contract opens them to agents, so no run erases what the reader keeps, serves or opens a folder outside its own or starts another run.

The prompt templates live in `assets/templates`: `catalog.json` names every template and the Markdown sections it joins in order, the sections every agent shares are written once, such as the principles, the way of planning, executing, verifying and reviewing, the formatting of code, security, interface, tests, version control, documentation, images and the report, and each platform or area adds its own section. Their text is written in English for the model, while their names and descriptions come from the catalog of the plugin, and inserting a template over a prompt the reader wrote asks before replacing it.

## Checking a plugin

The audits of `python3 make.py audit` refuse a plugin without its two files, a catalog missing a key in one language, a key outside the grammar, a key whose languages declare different arguments and a key nothing in the plugin reaches. The product suite boots every bundled plugin headless, builds their views and fails on any error written to the log.
