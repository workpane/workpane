# Architecture

Workpane is split into a C++ half that draws and a Lua half that decides. The C++ half owns the window, the frame loop, the components, the database and every platform service. The Lua half, the SDK in `lua/workpane` and the plugins beside it, owns every feature. The two meet only through host functions and events carrying JSON.

```text
┌──────────────────────────────── Lua on Varn ───────────────────────────────┐
│  plugins: ai, browser, code-editor, components, donate, flappy-bird, logs, │
│           system-information, task-hero, terminal, web-server              │
│  lua/workpane: bootstrap, loader, plugins, api, application, ui, rules,    │
│                preferences, events, reader, sounds, logs, task, process,   │
│                http, lifecycle, bridge                                     │
└──────────────── host functions ▲ JSON ▼ events and replies ────────────────┘
┌──────────────────────────────── C++ product ───────────────────────────────┐
│  scripting: ScriptRuntime, ApplicationHost, InterfaceHost, StorageHost,    │
│             SystemHost, FileHost, HttpHost, ProcessHost, AudioHost,        │
│             PluginRegistry, ReplyChannel                                   │
│  ui: Shell, SurfaceStore, components, Canvas, Widgets, Theme, Fonts, Icons │
│  persistence: Database, DatabaseExecutor, PreferenceStore, PluginDatabase  │
│  audio: AudioOutput, MiniaudioOutput                                       │
│  platform: PlatformWindow, dialogs, web views, system services             │
└──────────────────── Dear ImGui, GLFW, OpenGL, miniaudio ───────────────────┘
```

## Source layout

Every C++ file holds one class or struct named after the file, inside the `workpane` namespace, and every function and constant is a member of the class it serves. Folders follow the areas of the product: `app`, `audio`, `execution`, `files`, `http`, `json`, `localization`, `logging`, `persistence`, `platform`, `process`, `scripting`, `text`, `time` and `ui`, whose theme, model, components, Markdown and shell each have a folder of their own. Components are grouped by family under `ui/components`, such as `buttons`, `choices`, `collections`, `containers`, `indicators`, `inputs`, `pickers`, `settings`, `terminal`, `text` and `views`.

The shared platform classes are declared in `src/platform`, and each platform folder implements them in files named after the class they implement, such as `NativeViews.cpp` or `NativeSystemServices.mm`, beside its own classes, such as `LinuxWebView` or `WindowsText`. CMake compiles the folder of the platform it builds for, the `posix` folder that macOS and Linux share, and the time zone database that Windows and Linux read.

## Application and product

The executable runs `Application`, which owns what exists only because there is a window: GLFW, the OpenGL context, the ImGui backends and the frame loop. Everything else belongs to `Product`, the product without a window, which the application attaches to its window and which the test suite drives with the ImGui null backends and recording fakes of the platform services.

A start runs in three steps:

1. Opening the product selects the language of the system, takes the instance lock of the data directory, whose file says the product is running until it stops cleanly so a start after a crash tells the reader the workspace was recovered, opens the database, applies a staged import, sets aside a file that cannot be read, loads the preference documents, leaving out a damaged one with a warning in the log, and applies the stored language and theme through `CorePreferences`.
2. Attaching the window loads the fonts into the ImGui atlas, creates the texture cache, registers the components and builds the render context and the shell.
3. Starting creates the Lua runtime, registers the host functions, points the Lua path at the SDK, starts delivering log entries to Lua and loads the bootstrap, which hands the plugin manager of the SDK every plugin of the plugin folders, the bundled one first and then the ones the core preferences keep as `pluginFolders`, read once as the product starts. The manager installs the catalog of every plugin it discovered and starts every plugin the reader has not turned off, in the order of their dependencies. The registry accepts a plugin only from the directory named after it inside one of those folders.

A failure in any step is shown to the reader in a native alert, in the language the reader chose once the product read it and in the language of the system before, because a window that never opened has nowhere else to say it. The alert carries the reason the window system gave, and a display without OpenGL 3.2 is told apart as `window_opengl_unavailable`.

## Frame loop

The loop draws only when something changed. It waits without a deadline until the window system has an event, a background task posts work, work reaches Lua from one of its worker threads, the next timer of Lua is due, GTK has something to run for a web view on Linux, a native dialog needs polling, a toast needs to fade, a component asked for a frame at a known moment or the window has held a new place or size for half a second, which is when that geometry is written, as it is again on close.

Input, host calls made by Lua and work finished on another thread each wake the loop. Only input, a host call that can change the screen, such as a mount, a patch, a dialog or a theme, and a picture or a font that arrived ask for a frame, so a plugin that only writes the log, stores or reads wakes the loop without drawing. Every change is followed by two more frames, because ImGui learns the size of new content only in the frame that draws it and places its scroll bars in the next one. Animations such as the busy ring and the toggle ask for the next frame for as long as they move, a canvas with a frame rate asks for a frame at the moment of its next tick, and a window that just appeared, such as a tooltip or a menu, asks for frames until it has taken its size. ImGui spreads input that arrives at once, such as keys typed quickly or a click right after them, over several frames, so the loop keeps drawing while input waits in its queue.

On Linux the events of GTK run only inside the turn of the loop, so right before the loop sleeps the host asks the main context of GLib for its descriptors and its next timeout, a thread of its own waits on them and wakes the loop once one is ready or the timeout passed, and the loop runs what GTK has ready as it wakes, so a page open in a web view costs nothing while it is still.

A step of the wheel or of the trackpad reaches ImGui converted into the units ImGui scrolls by, five lines of the interface font down and two across, from the distance the platform scrolls for one step, which is ten points on macOS, where the window counts a trackpad in tenths of what the fingers travel, and three lines of text for each notch on Windows and Linux. The terminal, the sideways scroll area, the strip of tabs and the canvas read the distance of the wheel back through the same class, so every view moves as far as the views of the system.

A minimized window draws nothing, so every canvas, animation and view that draws only when shown pauses, while the loop still wakes for Lua, the queue of the main thread and the updates components run without drawing, and it draws again as soon as the window is restored.

## Threads

The interface thread draws, runs Lua and owns every component. Decoding images, listing the fonts of the machine and reading a font file run on a worker pool, a decoded picture keeps its pixels only until the renderer created its texture from them, and every database statement runs on one database thread in the order it was submitted. The audio device opens on a thread of its own the first time a sound is asked for, and miniaudio decodes each sound on its job threads. Work finished elsewhere comes back through the main thread queue, which wakes the loop, and asynchronous host requests answer Lua through the reply channel on that same thread.

## Lua bridge

A host function takes one JSON argument and answers `{ ok, value }` or `{ ok, error }`, where an error carries `code`, `message` and `detail`. Functions that finish at once answer directly. Functions that wait, such as a dialog, a database statement or opening a URL, receive a request number and answer later through the `workpane.reply` event, which the SDK turns into the future the plugin awaits.

The SDK keeps the only reference to the host table, which it takes away from the global environment the moment it loads, so a plugin reaches the host only through its own `workpane` API, and every call of that API carries the identity of the plugin that made it. The host refuses a call naming a plugin the registry does not hold, so code a withdrawn plugin left behind reaches nothing.

Plugins are isolated from each other inside one Lua state. Each one receives its own copies of the standard library tables, of every module it requires and of the lists the SDK answers, reads only the metatables it set itself, and acts only between its registration and its withdrawal. Events and capabilities carry copies of plain data between plugins, so no plugin ever holds a table, a function or a coroutine of another one.

| Event | Meaning |
| --- | --- |
| `workpane.view.open` | The shell shows a destination whose surface is not mounted yet |
| `workpane.band.open` | The workspace is ready and a band of a running plugin has no surface yet |
| `workpane.settings.open` | The settings view shows a section whose surface is not mounted yet |
| `workpane.ui.events` | Every component event of a frame, in one batch |
| `workpane.log.entries` | The entries the centralized log received since the last frame, in one batch |
| `workpane.shortcut` | A shortcut of the view on screen was pressed |
| `workpane.reply` | The answer of an asynchronous host request |
| `workpane.language.changed`, `workpane.theme.changed` | The reader chose another language or theme |
| `workpane.process.output`, `workpane.process.exit` | What a program of a plugin wrote since the last turn of the loop, and how it ended |
| `workpane.http.requests` | The requests a server of a plugin answered since the last turn of the loop |
| `workpane.shutdown` | The product is closing and plugins stop in reverse order |
| `workpane.close` | The hosts are about to be released, so the SDK refuses every later call with `bridge_closed` |

## Retained interface

Lua declares trees of nodes, each with a numeric identity, a kind, properties and children. The surface store keeps one tree per surface, named `view:<plugin>:<item>`, `band:<plugin>:<item>`, `settings:<owner>:<group>:<section>` or `dialog:<plugin>:<number>`, and a plugin may only touch surfaces carrying its own identifier.

Every property is read strictly, so an unknown property, a mistyped value or a value outside its range refuses the whole mount or patch with a structured error. A patch is read into the node itself and proven against all of its properties and its children, and a refused patch puts back every property it read, so a node is never left half changed and never keeps the JSON it was declared with. A kind whose properties ask for work beyond keeping them, such as the language of the code editor, does that work once the patch is accepted. An event that changes what its node shows, such as a check, a selection, a ratio or a tab dragged to a new place, names each property it changed with the field of its value that holds it, and the new order of the children it moved, which the node on the Lua side takes before its handler runs. A surface is bounded in depth and size, counted from its root and over all its nodes when children are replaced, so no plugin grows a tree past either bound one step at a time.

A component measures itself once per frame for a given width and draws inside the rectangle its parent assigns. After drawing, every mounted component gets an update of its own, so a component that owns work keeps up with it while it is hidden or its view is not on screen, which is how a terminal on a shelf keeps reading its shell. Replacing the children of a container keeps every node already inside it that the new children name again, wherever it lands, so a running terminal or a loaded page moves between containers without being built again. Events raised while drawing are collected and handed to Lua in one batch after the frame, so a handler never runs in the middle of a layout. The events that change state a node remembers, such as a changed value or a selected row, also update the node on the Lua side, so a later patch never puts back a value the reader replaced. A command sent to a node before its surface is mounted waits on the Lua side and reaches the component right after the mount, in the order the plugin sent it.

A canvas is the component through which a plugin draws anything the other components do not, such as a game. Its plugin sends a whole display list with the `draw` command, which the component validates command by command before it replaces the list on screen, and the canvas draws that list on every frame with the draw list of ImGui, clipping to itself and to the regions the list opens, and repeating a tiled picture across its rectangle so a floor of any length crosses the bridge as one command. Pictures come from the texture cache, decoded on the worker pool, and a pixelated canvas switches the sampler of the renderer to the nearest pixel around its pictures through the callbacks the OpenGL backend registers, restoring linear sampling after itself. A canvas with a frame rate reports `frame` through the batch of the frame at that rate while it is drawn, and the plugin answers with the next list, so a game advances by the seconds each frame reports and never by a timer of its own.

## Shell

The shell draws the bands of the plugins across the top and the bottom of the window, the mode bar on the left of what stands between them with primary destinations on top, secondary destinations at the bottom and the settings last, the current view on the right, the product dialogs over a dimmed window and the notifications in the bottom right corner above the bands of the bottom. A band takes the height its plugin declared or set later, which the registry keeps, and a band of height zero keeps its surface mounted without drawing it. The shell forgets the requests and failures of the surfaces of a plugin that left, so they are built again when it returns, and builds the preloaded views of a plugin started while the product runs at once. It shows a loading screen until every plugin has started. Every dialog it opens gets an identity of its own, so a field never inherits the text, selection or focus of a dialog closed before it. A dialog takes the height its content needs when it opens, bounded by the window, and keeps it, with its title and its buttons in place and the content scrolling between them, so a problem it reports never resizes it. Escape closes a list or a menu opened inside a dialog before it closes the dialog, the close key of the platform closes the dialog in front as it would close a window, and the destinations of the mode bar and the settings categories keep identities of their own even when they carry the same name.

The settings view lists every group alphabetically in the language of the reader, the groups of the core among them, and opens on the first one, shows every section of a group with its title in upper case in the accent, separates sections with one thin divider and filters sections by the texts of their titles and search keys.

The core shortcuts use the control key, which is Command on macOS: quit, open the settings, search the settings and switch to one of the first nine destinations. A core shortcut is routed over the item that has the keyboard, so a field, a canvas or a terminal never keeps it, unless that item keeps every key in that frame, as a terminal does with the control combinations it sends to its shell on Linux and Windows. A plugin declares the shortcuts of each of its views, the shell answers only those of the view on screen while no dialog is open, and the `workpane.shortcut` event runs the action in a task of the plugin.

## Plugin manager

The plugin manager of the SDK, `lua/workpane/plugins.lua`, keeps every discovered plugin with its state, which is `discovered`, `waiting`, `starting`, `running`, `stopping`, `disabled`, `unavailable` or `refused`, writes every change of state to the log under the category `plugins` with the state it left, the state it entered and the reason, and is the only code that starts and stops plugins. A surface the shell asks of a plugin while it starts is held and built once its start finishes, so a view built ahead of time, such as the workspace of the Terminal, comes back when the reader turns its plugin off and on. It installs the catalog of every plugin at discovery through the registry, so a plugin that does not run is still named in the language of the reader, and it resolves the order of the dependencies each time the set of running plugins changes, so a plugin whose dependency stops waits and starts again by itself once that dependency runs.

The switch of the reader runs one change at a time. Turning a plugin off stops its running dependents first, runs each `stop` for at most three seconds and withdraws each plugin, which forgets its events, capabilities, log subscribers, watchers of the reader, preference documents, servers, programs and sounds, unmounts its surfaces, closes its dialogs as cancelled and removes it from the registry, which takes its destinations, bands and settings off the shell. Each step runs on its own, so a step the host refuses is written to the log and never keeps the others. Turning a plugin on reads its folder again before it starts, so the code on disk is the code that runs, and the position the reader gave each switch is kept in the `pluginSwitches` preference of the core, which a plugin declaring `offByDefault` follows only once the reader turned it on. Erasing the data of a plugin that does not run drops everything under its prefix in one transaction on the database thread.

## Theme, fonts and density

A theme answers every color role, layout metric and font role, so no component carries a literal. The Green, Blue and Red themes share every color except the accent. The palette is grouped in families of surfaces, states, borders, scroll bars, text, the accent and the four tones, where each tone has its fill, the ink written over the fill, a subtle background and a readable text color, and roles such as the disabled text, the scroll bar shades and the destructive hover are defined by the theme instead of derived in code. Nothing outside the theme derives a color.

The interface is laid out in points multiplied by the scale of the monitor, which is the content scale divided by the framebuffer scale. On a Retina display the window works in points, the framebuffer has twice the pixels, and ImGui rasterizes every font at the density of the framebuffer. Text is always drawn with its face pushed, because ImGui applies that density only to the current face. Shapes follow the same density: ImGui measures the tessellation of curves and the width of the smoothed edge in its own units, which are points on a display of density two, so at the start of every frame the style sets the tessellation to a fifth of a framebuffer pixel and the edge to exactly one framebuffer pixel from the density ImGui reports, and a switch or a circle stays as round and as crisp as the text beside it. Inter Regular, Inter SemiBold and JetBrains Mono are bundled, and the italic faces are the upright ones slanted by FreeType.

A font size is an em size in points, as every other text renderer means it, while ImGui sizes a face by the height from its descender to its ascender. `Fonts` reads that height of every bundled face through FreeType when it loads them and answers the size ImGui rasterizes for an em size, which is about a fifth more than the em for Inter and a third more for JetBrains Mono. Inter runs about a tenth wider than the faces of the systems, so each role of the theme is sized one point smaller than a system face would be, while the terminal and the code editor keep the sizes the reader chooses because both monospace faces advance six tenths of an em.

A terminal or a code editor may name a monospaced family installed on the machine instead of the bundled JetBrains Mono, which answers to its own name. `FontFamilies` asks the platform for the monospaced families on a worker the first time a settings section or a component needs them, reads the file of a named family on a worker and hands its bytes to `Fonts`, which picks the face of the family that is neither bold nor slanted among the faces of the file, measures it like a bundled face and adds its four monospaced faces to the atlas. The component draws the bundled face until that frame, a family the machine lacks or cannot read stays in the bundled face, and the reason is written to the log once.

Every icon is a glyph of Lucide, bundled as `Lucide.ttf` with its license beside the text faces. The catalog maps each icon name of the product to one glyph and answers every other Lucide name from the table `LucideGlyphs`, which the `icons` task of `make.py` generates from the glyph names of the font and the audits compare with the font, and a glyph drawn at a size fills a square of that side, since the em square of every Lucide glyph is its whole drawing.

## Persistence

One SQLite database, `workpane.sqlite3`, lives in the data directory of the reader together with the instance lock. The core schema holds one preference document per owner and the schema version of each plugin. Plugin tables live in the same file, and an authorizer installed for each statement of a plugin allows only tables, indexes, views and triggers named with its prefix, a trigger only on a table of its plugin, lets SQLite reach its own schema tables only while it changes the schema of such an object and refuses pragmas, attachments, transactions and table renames. Every start compares the stored objects of each plugin with the ones its migrations create in memory, both ways, so a plugin with a stored object its migrations do not create is refused by name.

A preference document is a JSON object that belongs to one owner, the core as `workpane`, a plugin by its identifier or a named document of a plugin as `plugin:name`. `PreferenceStore` reads every document whole as the product opens, writes a document whole on the database thread and puts the committed document back when the latest write fails, and a stored document that is not an object is left out and told to the log instead of stopping the start. The core reads its own values through `CorePreferences`, which answers the language, the theme and the plugin folders by the same rules the core settings of Lua declare, and a value it refuses reads as its default. The Lua side declares every document with typed rules, reads a stored value its rule refuses as its default with a warning, applies a change in memory at once and writes the whole document with a revision, so the failure of the latest revision puts back the document the host committed, exactly as the host does.

Exporting the configuration writes a consistent copy with `VACUUM INTO`, and a destination that is the live database or one of its journals is refused with `configuration_export_live`. Importing validates the chosen file, refuses one whose plugin schemas are newer than the plugins installed here with `configuration_import_newer`, stages a snapshot of it taken with `VACUUM INTO` beside the database, so commits still in its write ahead log travel with it, and asks the reader to restart. Both transfers run one after the other on the database thread, and the two buttons of the Configuration section wait while one runs, and the next start swaps it in, keeping the previous file as a backup until the new one validates. A database SQLite reports as not a database or as corrupt, or whose schema or version is refused, is moved aside with a timestamp, and the reader is told where it went, while a busy lock or a failure to read or write the disk stops the start with its code and moves nothing. A failure to move or remove a database file while an import is swapped in stops the start before any database opens, and the database closes before the instance lock of the data directory is released.

## Audio

The interface `AudioOutput` is how the product plays the sounds of plugins, and `MiniaudioOutput` implements it with miniaudio on every platform, CoreAudio on macOS, WASAPI on Windows and the sound servers of Linux, which miniaudio loads at run time. The device opens on a thread of its own the first time a sound is asked for, so the interface thread never waits for it, and the sounds asked for meanwhile start once it runs. Each sound decodes on the job threads of miniaudio and starts when it is decoded, and the update of each frame answers the sounds that ended, which `AudioHost` forgets. A machine without a device and a file that cannot be decoded end their sounds at once with the reason, which the host writes to the log, once for the missing device. The suites play through a recording fake, and the platform suite plays a real file through the null backend of miniaudio.

## Platform

| Concern | macOS | Windows | Linux |
| --- | --- | --- | --- |
| Window | GLFW with Cocoa | GLFW with Win32 | GLFW with X11 |
| Application menu | Written by the product in the language of the reader, with a Quit that asks the window to close | None | None |
| Time zones | NSTimeZone | The time zone database of the C++ library | The time zone database of the C++ library |
| File access | access | _waccess | access |
| Monospaced fonts | Core Text | DirectWrite | fontconfig |
| Input methods | The text input protocol of AppKit, whose composition the product draws at the caret | IMM, which draws the composition at the caret | XIM, told the spot of the caret, whose input method draws the composition |
| Web view | WKWebView reparented into the window | A WebView2 controller in a child window, built in the background in one environment the first view starts | WebKitGTK inside a GTK plug embedded through XEmbed |
| Window a page opens | A WKWebView built from the configuration WebKit hands the delegate | A controller of the environment of its opener in a child window, handed to the request once its page script is in place | A WebKitWebView related to its opener in a plug of its own |
| Web data | A store named after the data directory from macOS 14, and the store of the application before | The user data folder under the data directory | The data, the cache and the cookies of one context under the data directory |
| Downloads | A navigation delegate turning a response into a download and a download delegate choosing its file | The download event of WebView2, handled instead of its flyout | The download signals of the context, with responses the page cannot show downloaded as well |
| Camera and microphone | The prompt of WebKit, with the usage descriptions and entitlements of the bundle | The prompt of WebView2 | A question to the owner of the view, answered by the Browser in a product dialog |
| Windows drawn over a web view | A layer mask and a frame that lets the pointer through | A window region | The bounding and input shapes of the plug window, set through the X shape extension because GDK never sends them for a plug inside a window it does not know |
| Native dialogs | portable-file-dialogs | portable-file-dialogs | portable-file-dialogs through zenity or kdialog |
| Opening a URL | NSWorkspace | ShellExecute | xdg-open |
| Data directory | Application Support | Local AppData | XDG data home |
| Terminal | forkpty | ConPTY | forkpty |
| Programs of plugins | fork and exec with pipes in a process group | CreateProcess with pipes inside a job object | fork and exec with pipes in a process group |
| Scrolling for one step | Ten points, as AppKit scrolls a trackpad and a wheel | Three lines of text for each notch | Three lines of text for each notch |

A field, the code editor and a terminal tell ImGui where their caret stands while they have the keyboard, and the application hands that place to the input method of the system, so its candidate window opens beside the caret. On macOS the text being composed comes back to the product, which draws it over the caret underlined in the accent until it is committed, and a key pressed while composing belongs to the input method, so it never deletes or submits text in the field. A caret that goes away ends the composition.

The webview library builds the page of each web view, and the platform view then drives that page directly, which is how a page opens a window of its own that keeps its opener and how the engine keeps the data of the pages under `web` in the data directory. Every page runs the page script, which posts through the message handler of the platform the address the reader opens in a background tab and the icon of the page drawn into a small PNG, and the view accepts only web addresses and PNG data addresses from it, since any script of the page can post.

Only HTTP and HTTPS addresses leave the product for the default browser, so no plugin can hand the platform a command disguised as a link. A program a plugin runs starts from an absolute path without a shell, its streams are read on a thread of its own and handed to Lua once per turn of the loop, and stopping it ends everything it started after a grace of two seconds. An image path of a plugin must stay inside its assets folder.

No descriptor of the product reaches a child, because every pipe is created closed on exec and a child on POSIX closes every descriptor above the standard three before it runs its program. The product ignores the broken pipe signal from its start, so a write to a peer that went away fails with an error instead of ending the process. Every pseudo-terminal starts with UTF-8 input, so erasing a character in a line the shell edits erases every byte of it. On Linux an opener of an address or a path is waited for only briefly and then left to a reaper of its own, so neither a worker nor a quit waits for the browser it started. On Windows a program runs inside a job that ends with it, its input is cancelled however it ends, and a descendant still holding its streams after the grace ends with the job. A native dialog or notification still open when the product quits closes with it. Every path the host answers is written with forward slashes on every platform.

## Tests

The suites link the same core library as the executable. Unit suites cover results, JSON reading, timestamps, localization, persistence and the authorizer, the HTTP server, the programs and terminals of each platform, colors, themes, icons, text case, Markdown and the layout. The scripting suite runs the SDK in a runtime of its own. The interface harness mounts real components in a headless ImGui frame and drives them with real mouse and keyboard input. The product suite boots the whole product with every bundled plugin against recording fakes of the platform and of the audio output, and the plugins with views of their own, the AI, the Browser, the Code Editor, the Terminal, the Web Server and the two games, each have a suite that drives their views, dialogs and settings, with programs, language servers, AI providers, MCP servers and web services played by the test. Module suites load the Lua modules of a plugin by themselves to check the rules they carry, such as the rules of Flappy Bird and the adventure of Task Hero driven tick by tick. Every product test fails on any error written to the log along the way.

## Packages

| Platform | Package | Layout |
| --- | --- | --- |
| macOS | Disk image | `Workpane.app` with the runtime library in `Frameworks` and the resources in `Resources`, signed after assembly |
| Windows | NSIS installer | `bin` with the executable and the runtime library, `share/workpane` with the resources |
| Linux | Debian package | `/opt/workpane/bin`, `/opt/workpane/lib` and `/opt/workpane/share/workpane`, a desktop entry, an icon and a `workpane` command |
