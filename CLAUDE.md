# Workpane Engineering Standard

This file is the authoritative engineering standard for Workpane. It records the product architecture, the ownership of every part, the implementation rules and the completion gates. A newer explicit product requirement takes precedence when it intentionally replaces a rule here, and this file is updated in the same change.

## Product

- Workpane is a native desktop workspace for developers that brings terminals, a code editor, a browser, local servers and AI agents into one window.
- The C++ half is the host and the Lua half holds every feature, each one a plugin the reader turns on and off or writes.
- The product supports macOS from 13.3, Linux and Windows on x86_64 and arm64, and the oldest macOS is set before the project of CMake, which the bundle declares as its minimum.
- The implementation uses C++20, CMake and Ninja through the task runner in `make.py`.
- Automated tests use GoogleTest with CTest discovery, and every test runs in a process of its own.
- Warnings are errors for every first-party target, with the full GCC and Clang warning set including conversions and shadowing.

## Repository organization

- The folder `src/app` holds the application, which owns the window and the frame loop, the product, which owns everything else and runs without a window in the suites, the command line, the core preferences and the build information.
- The folder `src/ui` holds the fonts, the icons, the painter, the textures and the widgets, with the theme in `ui/theme`, the components, nodes and surfaces in `ui/model`, the shell with its mode bar, dialogs, notifications and settings view in `ui/shell`, Markdown in `ui/markdown` and one folder per family of components under `ui/components`.
- The folder `src/scripting` holds the Lua runtime, the bridge and one host per area of the SDK, `src/persistence` holds SQLite, the preference documents, the plugin tables and the transfer of the configuration, and `src/platform` holds what each system does its own way.
- The folders `src/audio`, `src/execution`, `src/files`, `src/http`, `src/json`, `src/localization`, `src/logging`, `src/process`, `src/text` and `src/time` each hold the area they are named for, and `src/Result.h` and `src/Error.h` declare the result and the structured error every layer answers.
- The folder `lua/workpane` holds the SDK, `plugins` holds the bundled plugins in one folder per identifier, and `assets/fonts` holds the bundled faces.
- The folder `cmake` holds the pinned dependencies, their patches in `cmake/patches`, the sources of each platform, the staging of resources, packaging, warnings, sanitizers and coverage, and `extras` holds the images and the files of each platform package.
- The folder `tests` holds one folder per suite and the shared harnesses and fakes in `tests/support`, `docs` holds the references, `make.py` runs every task and `.github/workflows` holds the Build and Release workflows.

## Dependencies

- Every dependency is fetched by CMake from a pinned archive with its SHA-256, none is taken from the system except the platform libraries, and every patch is a CMake script in `cmake/patches`.
- Varn fetches CPM and every library it builds from archives pinned by their digests, so the digest of the archive of Varn pins every source the runtime builds, and the audits refuse any declaration or download of the product without its digest.
- A patched archive is extracted again whenever one of its patches changes, and the cache of every workflow is keyed on the patches, so a patch always meets the pristine sources it was written for.
- Dear ImGui draws the interface through GLFW and the OpenGL core profile, and FreeType rasterizes its text.
- Varn runs the Lua half, embedded as a shared library through its C interface, with its static dependencies built as position independent code.
- The web view is webview on macOS and Linux, patched to keep the data of its pages under the data directory, and WebView2 through the headers and the static loader of its SDK on Windows.
- The code editor is ImGuiColorTextEdit at a pinned commit, patched to center its rows, leave finding to the find bar of the product, keep the numbers of its markers and squiggles after a deletion, draw the guide of a block only beside the lines indented past it, move to the ends of a line with Command and the arrows on macOS, read only the lines its document still holds after the text shrinks and bound a pointer outside the window before it becomes a position.
- The terminal is libvterm, patched to keep faint text and OSC 8 hyperlinks, and GLFW is patched to let the product take part in the input methods of macOS and X11.
- The native dialogs are portable-file-dialogs, patched to start their helper programs through `posix_spawn` with the signals of a shell and no descriptor of the product, the sounds play through miniaudio, the icons are the Lucide font and the only persistence engine is SQLite.

## Architecture

- The C++ half owns the window, the frame loop, the components, the database, localization and every platform service, and knows no feature.
- The Lua half owns every feature, and the core application settings are the only Lua the product carries outside a plugin.
- The two halves meet only through host functions and events carrying JSON, and no pointer or implementation type crosses between them.
- The application owns what exists because there is a window, and the product owns everything else, so the suite composes exactly what ships.
- The frame loop draws only when something changed and otherwise waits without a deadline for an event or a wake, Lua wakes it when work reaches it from another thread and otherwise lets it sleep until its next timer, so no coroutine keeps it ticking, only input, a host call that can change the screen, a picture or a font that arrived and a component asking for a frame count as a change, so work that only logs, stores or reads never draws, every change is followed by the settling frames ImGui needs to lay out new content, including a tooltip or a menu that just appeared, and frames keep coming while input waits in the queue of ImGui, which spreads input that arrives at once over several frames.
- A minimized window draws nothing, so every canvas and animation pauses until it is restored, while Lua, the main thread queue and the updates of components keep running.
- The window remembers only the place it has on screen, written once it settles and on close, so a window closed while minimized opens again where the reader last saw it.
- The interface thread draws, runs Lua and owns every component, never waits for storage, a process, a worker or the network, and work done elsewhere comes back through the main thread queue.
- Every database statement runs on the database thread, and every image is decoded on the worker pool.
- A decoded picture keeps its pixels only until the renderer created its texture, so every picture costs its texture alone, a picture declaring more pixels on a side than the renderer accepts, with a smaller bound for a data address such as the icon of a page, is refused from its header before it is decoded, and a picture no drawn frame asked for over a bounded number of frames is released and decoded again when it shows.
- A host function answers `{ ok, value }` or `{ ok, error }` with a structured error of `code`, `message` and `detail`, and a function that waits answers through a request number and the reply event.
- A value Lua sends to the host nests within a bounded depth, since the runtime reads back fewer levels than it writes, and a reply or an event the runtime could not read is a structured error instead of a nil.
- The startup opens the product, attaches the window and starts Lua in that order, and a failure in any step is shown in a native alert in the language the reader chose once the product read it, and in the language of the system before.
- The window system is ended whenever it started, even when its window could not open, and every wake a platform service or the runtime holds is cleared before the product stops.
- A display without the OpenGL core profile the renderer needs refuses the window with `window_opengl_unavailable`, and any other refusal answers `window_create_failed`, each carrying the reason the window system gave.
- A second instance on the same data directory is refused before the database is opened, and the database closes before the data directory is released.
- A walk, a search or a listing of folders leaves out an entry it cannot read and says it is incomplete when a bound or a quit stopped it, and quitting stops a walk still running instead of waiting for it.

## Lua SDK and plugins

- The SDK in `lua/workpane` is the only code that reaches the host table, which it removes from the global environment as it loads.
- Every call of the API of a plugin carries the identity of that plugin, so no plugin can speak for another one, and the host refuses a plugin its registry does not hold.
- The identifier of a plugin is checked before its code runs and the core identifier is refused, and a plugin publishes, subscribes, provides, requests, reads the log and starts tasks only between its registration and its withdrawal.
- The bridge closes before the hosts are released, so a call that arrives later, such as one from a finalizer, answers a structured error.
- A plugin is a folder named after its identifier holding `plugin.lua` and `translations.lua`, with other files loaded through `include` and images and data kept in `assets`.
- Every plugin follows one layout: `plugin.lua` only declares the plugin and wires its start, stop and capabilities, `view.lua` builds its destination view, parts of the view layer live under `views`, `settings.lua` builds its settings sections, `preferences.lua` keeps the values they edit, `store.lua` owns its tables and every other module is named for what it is.
- Plugins are discovered in the bundled folder and then in the plugin folders the reader adds in the core settings, which take effect after a restart, and a plugin whose identifier a folder before it already answered is refused.
- A plugin environment receives its own copies of the standard library tables, of every module it requires and of the lists of `workpane.app`, the string metatable answers its name, the metatables of the SDK are locked and a plugin reads only the metatables it set itself, so no plugin can change a function or read a value another plugin uses.
- The SDK refuses a mistaken call with a structured error, never with an assertion.
- A plugin reaches only the standard Lua functions, a restricted `os`, the allowed Varn modules and its own API, and never another plugin.
- Plugins are ordered by their dependencies, started in that order and stopped in reverse order, and a plugin with a missing, refused or cyclic dependency is refused by name.
- When the product closes each stop runs for its share of a bounded time, so one stop that never ends holds the others only for that share, and the plugins reading the log stop after every other plugin, once what the other stops wrote reached them.
- A plugin that fails to register or start is withdrawn completely, with its destinations, bands, settings, subscriptions, watchers, preference documents, servers, programs, sounds, dialogs and surfaces, while its catalog stays so the Plugins section still names it.
- The plugin manager in `lua/workpane/plugins.lua` keeps every discovered plugin with its state of discovered, waiting, starting, running, stopping, disabled, unavailable or refused, writes every change of state to the log under the category `plugins` with the state it left, the state it entered and the reason, and is the only code that starts and stops plugins.
- A view, a band or a settings section the shell asks of a plugin while it starts is held and built once its start finishes, so a preloaded view comes back whole when its plugin is turned off and on, and the Plugins section shows the passing states while they last.
- Every switch of the Plugins section follows what the reader chose and waits while any change of the plugins runs, a plugin turned off stays off even when its code cannot be read, and closing the product waits for the change in progress before the plugins stop.
- The catalog of every discovered plugin is installed at discovery, and a plugin may declare a `descriptionKey` that the Plugins section shows under its title, which every bundled plugin declares.
- The reader turns any discovered plugin on and off while the product runs: turning one off stops its running dependents first, runs each stop for a bounded time and withdraws each one, turning one on reads its folder again and starts it and every plugin that waited for it, and one change of the plugins runs at a time.
- The position the reader gave the switch of each plugin is kept in the `pluginSwitches` core preference, a plugin declaring `offByDefault`, as the components gallery, Flappy Bird and Task Hero do, stays off until the reader turns it on, a plugin whose dependency does not run waits instead of starting, and the data of a plugin that does not run, installed or not, can be erased after a destructive confirmation.
- A plugin keeps its values through `workpane.preferences.define` with typed rules of `boolean`, `integer`, `number`, `string`, `list`, `record`, `map` and `any` that nest, validate a whole value with the path of its first problem and read a stored value they refuse as its default with one warning.
- A preference store answers get, values, set, update, reset, watch and control, applies a change in memory at once, writes the whole document with a revision and puts the committed document back when the latest write fails, exactly as the host does.
- A settings section edits a preference through the control its store builds, bound to the key both ways, instead of a hand written listener, and a plugin may keep several named documents.
- A plugin follows the capabilities it uses with `workpane.capabilities.watch` and the language and theme of the reader with `workpane.app.watch`, instead of reading them once.
- A plugin contributes bands across the top or the bottom of the window, each built as a surface `band:<plugin>:<item>` once the workspace is ready, with a height of zero or within the bounds the shell declares, which its plugin changes with `workpane.shell.resizeBand`.
- Focus mode of the Terminal zooms the focused terminal, so zooming one focuses it and another terminal taking the focus ends the zoom, and the Terminal announces a change of its workspace only when the snapshot it offers changed.
- The Web Server keeps a bounded list of the newest requests of each running server, newest first, renders them once for every batch that arrives in one turn of the loop, and treats a link to a terminal that closed as no link.
- A plugin plays WAV, FLAC and MP3 files of its assets through `workpane.audio`, the audio thread opens the device and every file, a sound that cannot be heard ends at once with its reason in the log and never fails the call, and the sounds of a plugin stop with it.
- The Flappy Bird plugin is a game in a destination and the Task Hero plugin an adventure that runs by itself in a band at the bottom, and each keeps its rules in a module without drawing that the suite drives tick by tick.
- In Flappy Bird the canvas stops ticking while a round is paused and once the board of a round that ended can start the next one, and the board follows the best rounds as they are recorded or forgotten.
- In Task Hero the knight keeps a fixed place near the left edge so a resize only widens or narrows the map, foes enter from beyond the right edge and wait in a line where their pictures never overlap, every attack plays whole with its blow landing on the impact frame of its sheet, every death leaves smoke, and the scenery stands on a line behind the figures.
- In Task Hero every pose stands on the pivot of the shadow the pack draws, a foe is mirrored about its own middle, an item falls only after the knight walked far enough since the last one, at most two wait ahead at once a gap apart, and whatever wholly leaves the band on the left is removed, a foe, an item, a piece of scenery or a cloud.
- In Task Hero meat fills the health of the knight in a glow of healing, whether he picks it up or beats a sheep for it, and an emblem turns him into a warrior, a lancer or an archer kept in his progress, the ground reaches the bottom of the band at every height and a paused adventure stops ticking.
- Task Hero writes the progress of the knight at most once per bounded stretch of adventure, at once when the adventure pauses and when the plugin stops, and the Code Editor writes its folders and documents shortly after a change and waits for that write when it stops.
- In Task Hero the counters keep a gap between the emblem, the coin, the gold and the level, measured with `workpane.ui.textWidth`, and their box widens to hold them while the moon stays left of it.
- In Task Hero an arrow rises from the bow and comes down on the spot its target stood on when it was loosed, turning along its path and crossing the reach of an archer in a short flight.
- In Task Hero day and night stays off until the reader turns it on, and then the night follows the clock of the computer from seven in the evening to seven in the morning, as in Flappy Bird, with a sky shaded in bands, twinkling stars, a moon, fainter clouds and a shade over the world that leaves the health bars and the counters above it, while the day and a band with it off keep the scene of the pack, and every bar is framed in the dark outline of the pack.
- The sprite sheets a plugin bundles are cut to the frames it draws, with the shared box of the visible pixels of every frame, so no texture carries empty pixels or frames nobody draws.
- Anything that waits answers a future, whose `await` answers the value and a failure, and `workpane.await` raises the failure instead.
- A future carries its reply beside the Varn deferred it waits on, because the Varn resolver takes no value and a Varn promise flattens a structured error into a message.
- Every handler, view builder and task runs in a protected coroutine, and a failure is written to the log of its owner instead of ending the runtime.
- Plugins talk to each other only through events, whose topics start with the identifier of the sender, and capabilities, which have exactly one provider, and every payload and value crosses between them as a copy of plain data.
- The centralized log takes entries from any thread and keeps them until the interface thread takes them, calls its consumer once per batch and never after its delivery is cleared, and reaches Lua once per frame as a batch of entries, kept in a bounded backlog until the first subscriber, a subscriber that fails is removed after its first failure and none writes to the log it reads, and the Logs plugin is what stores them.
- The components gallery ships in every build and stays off until the reader turns it on, so anyone can check every component on their own machine.
- The AI plugin reaches the client a server has when a call runs, withdraws at the server a request past its deadline or no longer awaited, gives a command line agent its prompt on standard input where its provider reads it there, keeps every block of reasoning with its own signature or redacted data, and tells the reader why a run failed in their language while the English message stays in the log.
- The AI plugin answers every conversation, of its agents and of any plugin through `ai.chat.complete`, from one canonical list of messages with typed parts, checked whole, fitted to the traits of the model with a note for what it cannot read and only then written in the protocol of its provider.
- The chat of an AI task follows the size its zoom buttons keep in the settings of the plugin, shows the text of a turn once while its tools run, draws a summary as a note of the conversation and keeps in the composer a message it could not record.
- The reference for writing a plugin is `docs/plugins.md`, updated with every change to the API.

## Components and shell

- Every component validates every property strictly, and an unknown, mistyped or out of range property refuses the whole mount or patch.
- A patch is read into the node itself, proven against all of its properties and its children, and put back whole when any check refuses it, so a refused patch leaves the node exactly as it was, and no node keeps the JSON it was declared with.
- Events raised while drawing reach Lua in one batch after the frame, and an event that changes what its node shows names each property it changed with the field of its value that holds it, and the new order of the children it moved, which the node on the Lua side takes whatever its kind.
- A surface is named `view:<plugin>:<item>`, `band:<plugin>:<item>`, `settings:<owner>:<group>:<section>` or `dialog:<plugin>:<number>`, and only the plugin it names may touch it.
- A component measures itself once per frame for a given width, and a layout already built for a wider width is reused when it still fits.
- A component given a width keeps it in a column, a grid, a form field and a settings row instead of stretching.
- Children that do not grow keep their content size, growing children share the free space by their factors and give back a shortfall down to their minimum size, passing the rest on once one stops there, and a container that grows nothing justifies its children.
- A stretched child keeps within its own bounds, and a component given less than a pixel draws nothing, because ImGui reads a size of zero or below as the rest of the window.
- A component outside the visible part of its window is laid out but not drawn, unless it takes the keyboard, brings itself into view or the keyboard moves through items, a paragraph measured and drawn in one frame is broken into lines once, and a tree, a table and a menu build what they draw once and only while it shows.
- A row without room for every child hides its children with a collapse above zero, the highest first, until the rest fits.
- A component that holds a native resource releases it when it is detached.
- Every mounted component gets an update after each frame, and a component that owns work does it there whether or not it was drawn.
- Replacing children keeps every node already inside the container that the new children name, wherever it lands, so state such as a running shell is never built again, and properties given with the children change in the same step, so a strip of tabs changes its tabs and its pages at once.
- A surface nests within a bounded depth below its root and carries a bounded number of nodes, counted over the whole surface when children are replaced.
- Any component can be dragged when it declares a drag value, and only the innermost enabled target that accepts its kind receives it.
- A terminal keeps every key while it has the keyboard but never the pointer, so any other control answers the first click, and the find bar of a terminal or a code editor takes the pointer where it is drawn.
- A terminal reports the buttons of the reader and one wheel report for every three lines to a program that asked for the mouse, opens its own menu with a secondary click otherwise, reads its shell in bounded slices and keeps the shell and history file its plugin gives it.
- A closed terminal hangs up its shell before anything waits for it and ends it on its own thread, so closing never waits for a shell and the host waits for the shells still ending only when the product ends, and on Windows a shell that ignores the closed console ends after its grace and the last output drains within a bound, so a program still attached never holds the quit.
- A shell never receives what a terminal of Workpane exported for its own shell, such as its history file, its `WORKPANE_` variables and its zsh integration folder, so a product started from such a terminal still loads the configuration of the reader.
- A terminal reports the end of its shell as soon as the shell ends, even while a program the shell left in the background still holds the terminal, through a descriptor of the end of the process on Linux and the revoked terminal of the session on macOS.
- Each terminal keeps its commands in a history file of its own, which PowerShell learns through PSReadLine and a POSIX shell through its history variable, while the command prompt of Windows keeps no history file, the history of a terminal the reader closed is removed the next time the workspace loads, and closing the last tab of the Terminal leaves an empty view that opens a new workspace instead of opening one by itself.
- The cursor of a terminal stays steady unless the reader turns blinking on in the Terminal settings and picks a slow, normal or fast speed, and a blinking cursor blinks only while its terminal has the keyboard and its program did not ask for a steady one, starts lit at every key and rests lit after a while without one.
- A terminal starts its shell the way a terminal of its system does, a login shell on macOS, an interactive shell on Linux, PowerShell with the profiles of the reader on Windows and a login shell for a POSIX shell there, so every startup file of the reader runs.
- The code editor answers only the latest completion request, once, and keeps the proposal the reader chose while the word does not change.
- The Code Editor plugin polls only the folder in front, starts a restored folder once the editor is first shown, asks its servers from the text on screen, reads the output of a server in the order it came with every handler in a task of its own, and restarts only a server whose program changed.
- A search of the code editor or of the terminal reads a bounded part of the document or of the history at each frame and asks for the next frame until it ends, the completion list draws only its rows on screen and asks for a frame only when a key moved it, and an elided text is cut with one measurement.
- The code editor shows one tooltip of the product at a time, draws a caret as wide as the caret of the theme and uses the Dark Modern scheme unless its plugin gives one.
- The code editor draws rows one and a half em tall with centered text, a glyph margin before right aligned numbers and one glyph before the text, the current line filled under its text, markers as wavy underlines in their tone whose messages show only in their tooltips, never as tinted lines, boxed numbers or text after the code, and the guide of a block only beside the lines indented past it, so a namespace draws none.
- A marker covers a range, the glyph margin shows the icon of the strongest tone starting on a line, its tooltip on the text lists only the markers under the pointer while the margin and the number list every marker starting there, and each one shows its tone, message, origin, position and related places.
- The language client declares related information, so a server sends the notes of a problem as its related places instead of repeating them in the message and as problems of their own.
- The outline and the symbol search show each symbol with the icon of its kind in the tone of its family, types in the warning tone, callables in the information tone and values in the success tone, and the language client declares every kind the protocol numbers.
- A web view is hidden while a product dialog is open, because a native view is drawn above everything the product draws, and it cuts out the tooltips, menus, lists and notifications drawn over it, for drawing and for the pointer.
- A window a page opens is shown by another web view that adopts it once its page script is in place, so it keeps its opener and its first document already runs that script, and the page script posts only web addresses and PNG icons, which the view checks again, from a script world of its own that no script of the page reaches on macOS and Linux, while every platform drops the opens that pass a bounded rate.
- A web view names the browser of its engine to pages, Safari with the installed version on macOS, where WebKit leaves the browser out for an application, and lets a page show an element in full screen on every platform.
- A web view saves a response the page cannot show, one sent as an attachment and a link marked for download in the downloads folder the platform names, through `DownloadTarget`, under the suggested name made unique, and reports `download`, and the Browser tells the reader where the file went or which one failed.
- A page asking for the camera or the microphone meets the prompt of the engine on macOS and Windows, and on Linux the view asks its owner with `permission-request` and waits for `answer-permission`, which the Browser answers after a product dialog, and a view that goes refuses every question still waiting.
- Passkeys in the web view need the entitlement Apple grants to web browsers on macOS, so the product claims them only on Windows until that entitlement is signed into the bundle.
- A dialog asked for while another one is open is drawn above it as a nested modal and answered first, and frames keep coming until the window behind it is fully dimmed.
- A dialog keeps the size it takes when it opens, bounded by the window, with its title and buttons in place and its content scrolling between them clear of the scroll bar, and a button that checks a form reports its press instead of closing.
- A form writes each label above its field followed by a colon, and the settings view alone keeps its captions on the left, with each control against the right edge and its hint under it, every line ending where the control ends.
- The shell draws the bands of the top and the bottom across the whole window, the mode bar and the current view between them, the dialogs and the notifications above the bands of the bottom, and knows no destination or band until a plugin contributes it.
- The shell forgets the requests and failures of the surfaces of a plugin that left, so they are built again when it returns, and a band that fails shows the translated failure message in its place.
- A canvas draws the display list its plugin sends, validated command by command with the index of a refused one, ticks at its frame rate only while it is drawn, samples its pictures by the nearest pixel when it is pixelated and keeps every key while it has the keyboard except the combinations of the product.
- A canvas holds the pictures its plugin names from the frame it mounts, so each one is decoded before the first frame that draws it, and lets them go when it leaves, as it does when its plugin turns off, and Flappy Bird and Task Hero name every picture they draw.
- A canvas repeats a tiled picture across the rectangle of one command, so a floor or a wall of any length costs one command on the bridge.
- A canvas samples a frame of a sheet just inside its edges, so the frame beside it never shows along an edge, mirrored or not, whatever position and scale the figure takes.
- A notification waits for the workspace and lasts its lifetime from the first frame that shows it, so none is spent over the loading screen.
- The settings view lists every group alphabetically in the language of the reader, the groups of the core among them.
- A step of the wheel or of the trackpad scrolls every view as far as the system scrolls its own, ten points on macOS and three lines of text for each notch on Windows and Linux, handed to ImGui in the units it scrolls by and read back through the same class by every view that scrolls by itself.
- The Plugins group lists every discovered plugin with its state and switch, offers to erase the data of a plugin turned off beside its switch, lists removed plugins that left data only while there is such data, and keeps the plugin folders in a section of their own.
- Every quit goes through one translated destructive confirmation, and a window that never finished loading closes without asking.
- A core combination is routed over the item that has the keyboard and answered unless that item keeps every key in that frame, so a canvas never swallows it even when the key and its modifier arrive in one frame.
- Shortcuts use `mod` for Command on macOS and Control elsewhere, a plugin shortcut belongs to one view and is answered only while that view is on screen, even while one of its fields has the keyboard, and a combination the core keeps, including the text editing chords, or one that would take typed characters is refused.
- A secondary click opens the menu of the innermost component under the pointer that declares one, and a menu reads the clipboard once when it opens.
- Tooltips, menus and lists always open inside the window, turning above or toward the left of their control when it has no room, a tooltip is the dark panel of the theme, and a pointer resting while the keyboard navigates shows no tooltip until it moves again.
- The reference for every component is `docs/components.md`, updated with every change to a component.

## Visual standard

- The Green, Blue and Red themes share every color and differ only in the accent.
- A component never carries a literal color, metric or font, and names the roles of the theme instead, except the colors a plugin draws on a canvas, which are its own content.
- Dividers are single lines one point wide, never doubled, and a strip of tabs without pages leaves the line under it to its container.
- A control that draws its own focus border hides the navigation ring of ImGui, so focus is never drawn twice.
- Something the reader must see is an alert, on the background of its tone with one rule of that tone down its left edge, and danger is the tone of an alert that names none.
- The theme names a color for every case in families of surfaces, states, borders, scroll bars, text, the accent and the tones, each tone with its fill, the ink written over the fill, a subtle background and a readable text color.
- Text over a fill is written in the ink of that fill, every theme is tested for the contrast of each pair, and nothing outside the theme derives a color.
- A fill darkens under the pointer and further while pressed, so its ink never reads worse than at rest, and a link lightens toward the readable text of its tone.
- Section titles are written in upper case in the accent, and page headers close with one divider.
- Text is drawn with its face pushed, so ImGui rasterizes it at the density of the framebuffer and it stays crisp on every high density display.
- The layout is expressed in points multiplied by the scale of the monitor, and the style is applied again when the window moves to a monitor of another scale.
- Inter and JetBrains Mono are the bundled faces, and italics are the upright faces slanted by FreeType.
- A font size is an em size in points, and the fonts translate it into the height ImGui rasterizes.
- Every reading size a reader chooses, in the terminal, the code editor or the chat, starts at 11 points.
- Every icon is a glyph of the bundled Lucide font whose em square fills the square it is given, drawn with its face pushed so it is sharp at every density.
- Curves are tessellated to a fifth of a framebuffer pixel and edges are smoothed over exactly one framebuffer pixel, set each frame from the density ImGui reports, so a switch or a circle is as crisp on a display of density two as the text around it.
- An icon is named by the product or by its Lucide name, a name of the product wins, and the table of Lucide names is generated from the font by the `icons` task and audited against it.
- The components gallery is the visual reference, and every component appears in it in every state it has.

## Persistence

- One SQLite database in the data directory holds the preference documents, the schema version of every plugin and every plugin table.
- A preference document belongs to one owner, the core, a plugin or a named document of a plugin written `plugin:name`, is read whole at startup and written whole on the database thread, a failed write puts the committed document back, and a stored document that is not an object is left out with a warning instead of stopping the start.
- The core reads its language, theme and plugin folders through `CorePreferences` by the rules the core settings of Lua declare, and a value it refuses reads as its default.
- A plugin table is named with the plugin identifier and two underscores as its prefix, an identifier never holds two hyphens in a row so no prefix starts another one, and an authorizer installed for every statement of a plugin enforces it.
- The authorizer lets a plugin change its schema only in its migrations, creating, altering and dropping tables, indexes, views and triggers under its exact lowercase prefix, a trigger only on a table of its own and a foreign key only to a table of its own, lets SQLite reach its own schema tables only while it changes the schema of an owned object, refuses pragmas, attachments, transaction commands and table renames, and interrupts a plugin statement and a migration that pass their bounded times and any plugin statement still running as the database closes.
- A plugin transaction rolls back only while it is still open, so a failure SQLite already rolled back reaches the caller as itself, and a binding or a stored real that is not a finite number is refused both ways.
- Plugin migrations are lists of statements a plugin declares in its definition, which the manager applies before every start once, atomically and in order, a stored version newer than the list is refused, and the stored tables, indexes, views and triggers of a plugin are compared both ways at every start with the ones its migrations create in memory, so a stored object its migrations do not create refuses the plugin by name instead of failing a statement later.
- Erasing the data of a plugin drops every object under its prefix, its schema version and every preference document it owns in one transaction on the database thread, and is refused while the plugin runs.
- Exporting writes a consistent copy with `VACUUM INTO` beside its destination and moves it into place only once whole, never over the live database, and importing stages a validated snapshot the same way, which the next start swaps in only after checking again that it is whole, so a staging cut short never replaces the database of the reader, and an import is refused when it carries a plugin schema newer than the number of migrations the installed plugin declares, whether or not that plugin ever ran here.
- A failure to move or remove a database file while a snapshot is swapped in stops the start before any database opens, so an import never loses the database of the reader.
- A database SQLite reports as not a database or as corrupt, or whose schema or version is refused, is moved aside with a timestamp and the reader is told where it went, while a busy lock or a failure to read or write the disk stops the start with its code.
- No code reads or converts a stored layout other than the one the migrations of its owner create.

## Localization

- The product speaks English and Portuguese, starts in the language of the system and switches at once without restarting.
- A translation key has three lowercase parts of letters, numbers and hyphens, and its first part is its owner.
- Every language spells every key with the same numbered arguments, and a catalog breaking either rule is refused when it is registered.
- A component shows a translation reference that is resolved again whenever the language changes, so views never rebuild for it.
- Every text a reader sees comes from a catalog, and only proper names, file names and identifiers are written literally.
- Locales are written in lowercase, such as `pt-br`.

## Platform

- Linux runs GLFW on X11, because the web view embeds WebKitGTK through XEmbed, and Wayland sessions run it through XWayland.
- On Linux a thread of its own waits on the descriptors and the next timeout of GLib while the frame loop sleeps and wakes it once GTK has something to run, so a web view never keeps the loop turning.
- On Linux the numeric locale goes back to C once GTK takes the locale of the reader, so the product and Lua always read and write numbers with a point, and only outline faces without color are offered as monospaced families.
- The input method of the system opens its candidates at the caret of the focused text, and the text it composes is drawn there by the product on macOS and by the system on Windows and X11.
- Only HTTP and HTTPS addresses leave the product for the default browser.
- Every program, shell and system helper starts through `posix_spawn` on macOS and Linux, without copying the product, in a process group or a session of its own set as it starts, with every signal unblocked, a broken pipe ending it again and no descriptor of the product but its three streams, and a shell leads a session whose controlling terminal is the one it opens.
- The product ignores SIGPIPE from its start, so a write to a peer that went away fails with an error instead of ending the process.
- Every pseudo-terminal starts with IUTF8, and the startup files a terminal writes for zsh are replaced whole and only when they differ.
- A Windows program runs inside a job that ends with it, its input is cancelled however it ends, and the job ends before its writer is joined.
- A native dialog or notification still open at quit closes with the product, and an opener Linux starts for an address or a path is waited for only briefly and then left to a reaper of its own.
- Every path the host answers is written with forward slashes on every platform, where a network share of Windows starts with two of them, and every path becomes text through `PathText`, which replaces what UTF-8 cannot carry, so a Windows name holding a lone surrogate never ends the product.
- A file is moved over another one through `FileReplacement`, which on Windows tries again for a bounded time while another program holds either file, as a scanner does right after a write.
- A plugin judges whether a path is absolute and converts between paths and `file:` addresses only through `workpane.files.absolute`, `uri` and `path`, which follow the rule of the platform the host checks again, so a network share of Windows is absolute and its server is the host of its address.
- The macOS bundle allows any load inside web content, because the browser reads plain HTTP pages such as the ones a local web server answers.
- The Windows executable and the suites declare UTF-8 as the code page of the process, so every narrow path and text handed to Windows is UTF-8.
- The Windows executable belongs to the windows subsystem and starts at `main`, so it opens no console of its own, and the help and the version it prints reach the console of the terminal that started it unless a pipe or a file takes them.
- A page never reaches the handler the web view library registers for its own bindings, because the product binds nothing and that handler trusts what a page posts.
- On Windows every web view is built in the background in one environment of WebView2 the first view starts, so building one never holds a frame or pumps messages inside it, the address it was given opens once its page script is in place, and a view that could not be built reports once in place of its page.
- Text read from Cocoa is converted with a replacement for what UTF-8 cannot carry, so a lone surrogate a page sets never ends the product.
- An image path of a plugin must stay inside its assets folder.
- Platform code lives in `src/platform/<platform>`, what macOS and Linux share lives in `src/platform/posix`, CMake selects the folders of the platform it builds for, and shared code never tests the platform except where the behavior itself differs.
- A platform folder implements a shared declaration in a file named after that class, such as `platform/linux/NativeViews.cpp`, and keeps its own classes in files named after them, such as `platform/linux/LinuxWebView.h`.

## C++ implementation standard

- Follow the patterns already in the project, and keep headers, implementations, namespaces, names and ownership aligned.
- Code is compact, professional and consistent, written as an experienced C++ product engineer writes it, without excess vertical space or artificial abstractions.
- Write the new and final version only, without workarounds, fallbacks, legacy paths or compatibility with earlier behavior, and remove or refactor whatever that requires.
- A change is made because the product needs it, never to show activity, and nothing unused stays: no dead code, unused API, speculative public member or unbuilt source.
- Every C++ file holds one class or struct named after the file, whose header declares it and whose source defines only its members.
- Every function and every constant is a member of the class it serves, and what only that class uses is a private static member of it.
- A behavior two classes share belongs to the type it describes or to a class named for that responsibility, and no class is a helper, utility or bag of loose functions.
- A small type used only inside one class is a private nested type of that class, defined in its source when its header does not need its layout.
- An enumeration lives beside the type that owns its meaning, and a vocabulary several types share, such as an axis or an identifier alias, has a header of its own.
- Everything is declared inside the `workpane` namespace, and only the entry point in `src/main.cpp` stands outside it.
- Related classes share a folder, such as `ui/theme`, `ui/model`, `ui/shell`, `ui/markdown` and one folder per family under `ui/components`.
- The anonymous namespace is never used, so every constant, type and function belongs to a named namespace.
- A member variable carries the `m_` prefix and no identifier begins or ends with an underscore.
- Values, `const`, references, RAII and smart pointers are preferred, and unsafe casts, owning raw pointers and avoidable macros are prohibited.
- Early returns reduce nesting and no `else` follows a branch that returns, continues or breaks.
- Validation, mutation, side effects and returns are separated by blank lines, so a method reads as a beginning, a middle and an end.
- Blocks of different responsibility are separated by one blank line, and independent conditions, validations, loops, mutations and returns are never glued together.
- Nesting is kept shallow, and a condition that ends the work leaves early instead of wrapping the rest.
- A complex block carries a short comment of intent, and a helper is extracted only when it is a cohesive responsibility rather than to shorten a method.
- Closed value sets are validated explicitly, and an invalid value produces a structured error rather than a silent default.
- Every result a caller may not drop is declared `[[nodiscard]]`, and a result that is deliberately ignored is assigned to `std::ignore`.
- Nothing in the product ends the process, so no assertion, exception or termination answers a state the code can handle.
- Includes are one group for the header of the file, then project headers, then third party headers and then the standard library, each sorted by the formatter.
- Every file includes the headers of what it names, and a source may rely on what its own header includes.
- Every call, declaration and statement stays complete on one physical line, and the formatter uses an unlimited column width.
- A reader of JSON never holds a temporary, because it keeps a reference to what it reads, and it answers an object, a list or any value as a pointer into what it read, so reading never copies a tree.
- The code uses only what every toolchain of the workflows implements: threads that stop are plain threads with an atomic flag or a condition instead of `std::jthread` and `std::stop_token`, a double is never converted with `std::from_chars`, both of which the libc++ of the macOS runners lacks, and a JSON value is compared with the text of a view as JSON, which the compiler of Microsoft finds ambiguous otherwise.
- A number the product writes or reads for the reader uses a point before its decimals whatever the locale of the process.
- A reader of JSON remembers the fields it read by the values it found, so reading an object never copies the names of its fields.

## Comment standard

- Every comment is a complete sentence that starts with a capital letter and ends with a period.
- A sentence that would start with an identifier written in lowercase is rewritten so the identifier is not at the start, and the identifier keeps its exact spelling.
- The comment above a function, method, class or module says what it does for whoever calls it, never how it is implemented inside.
- A header carries a comment above a class, struct or enum when one helps, and never a comment describing methods, sections or members.
- A sentence never spans more than one line and never continues on the next one.
- A comment needing more than one sentence ends the current one with a period before the next one begins on the following line.
- Comments are objective and natural, never verbose, fragmented, narrative, decorative or section labels, and never repeat what the code already says.
- A comment sits on what it explains, so it is never followed by a blank line, and no comment divides sentences with a semicolon.
- Code and comments are written in English.

## Lambda formatting

- Every C++ lambda is formatted by hand and enclosed by `// clang-format off` and `// clang-format on`.
- Those markers are formatter directives rather than comments and stay lowercase.
- A complex lambda passed to a function is assigned to a named local first, so the call stays complete on one line.

## Lua standard

- Lua follows the same comment standard with `--` comments.
- Every module answers a table, and nothing is added to the global environment.
- A value that crosses into C++ is validated there again, so Lua never relies on the host trusting it.
- An empty Lua table crosses the bridge as an empty object, so the host reads an empty object as an empty list wherever it declares a list.
- A plugin writes every text it shows through its catalog, and every key of its catalog is reached by its code or composed from a family its code names.

## Automated testing standard

- Every production behavior has automated coverage for its success, failure, boundary and lifecycle paths.
- The suites live in `tests/core`, `tests/http`, `tests/localization`, `tests/persistence`, `tests/platform`, `tests/ui`, `tests/scripting` and `tests/product`, and link the same core library as the executable.
- Shared test code lives in `tests/support` with one class per file, and what only one suite uses belongs to the fixture of that suite.
- The interface harness mounts real components in a headless ImGui frame with the real fonts and drives them with real mouse and keyboard input.
- The product suite boots the whole product with every bundled plugin against recording fakes of the platform, reads what the plugins declared from the messages that cross the bridge, which the runtime lets an observer see, and fails on any error the Logs plugin stored.
- Every new component gets a sample in the component suite, every new plugin behavior gets a product test and every bug fixed gets the test that would have caught it.
- A test asserts on structured error codes rather than on messages.
- The fakes of the product suite answer paths that are absolute on the platform the suite runs on, the product tests compare paths in the form the host answers them, with forward slashes, and a failed product test prints the errors the product logged.
- A test that only needs a native window creates it without an OpenGL context, and the one test that opens the window of the product skips only where the machine answers `window_opengl_unavailable`, as the runners of macOS and Windows without a graphics device do.

## Development workflow and commands

- The build task `python3 make.py build` builds, the run task `python3 make.py run` runs, and a value after `run` is a data directory for an isolated run.
- The resources are staged again whenever a file under them is added, changed, removed or renamed, so the staged tree always matches the sources.
- The test task `python3 make.py test` builds and runs every test, and the task `python3 make.py all` checks formatting, runs the audits and the tests.
- The format task `python3 make.py format` formats every C++ source, and `python3 make.py format-check` fails on any difference.
- The icons task `python3 make.py icons` writes the table of Lucide names from the bundled font, and the audits refuse a table that differs from the font.
- The version is declared once in the project of CMake, which the bundle, the packages, the installer, the build information and the settings read, and the version task `python3 make.py version` prints it or sets a new `MAJOR.MINOR.PATCH`.
- The audit task `python3 make.py audit` enforces for Lua and for the task runner the same separation of blocks as for C++, where a comment above a block belongs to the block and a lambda or a function passed as an argument that closes counts as a closed block, a blank line between the return that ends a function and the mutations and side effects before it, blank lines never against the words that open or close a Lua scope, no else after a Lua branch that leaves, and Lua modules that add no global and answer a table they declare, and it enforces the lambda markers, the comment standard, the header comment rule, named namespaces without catch-all names, one class per file, functions and constants that belong to a class, the absence of helper classes, the workpane namespace in every file, listed sources, methods somebody calls, include groups, statements on one line, scopes without blank lines against their braces, the absence of `else` after leaving, the plugin catalogs, the documented commands, the prose rule and the marking of every command, option, path and identifier named in a comment, a document or a translation.
- The lint task runs the audits and Cppcheck, the sanitize task runs the suite with the address and undefined behavior sanitizers over the product and the libraries it compiles and patches, GLFW, the code editor and libvterm, while Varn, a shared library with dependencies of its own, runs as it is, where any finding ends the case, and the coverage task writes the coverage report.
- The package task builds the release package, and the validate-package task reads only the package of the current version and proves it carries the executable, the runtime library, the resources and every bundled plugin and that its executable answers its version.
- A change is finished only when the build has no warning, every test passes and both the formatting check and the audits pass.
- Every commit message is one short and objective line written in lowercase, opened by a prefix that says what the change is, such as `feature:`, `fix:` or `docs:`, as in `fix: keep the tree whole when its filter is cleared`.
- No commit, commit message, pull request or release note mentions Claude Code or any other AI agent, and none carries a co-author line naming one.

## Continuous integration and packaging

- The Build workflow checks formatting and audits once, then builds, tests, packages and validates on Linux, macOS and Windows for x86_64 and arm64.
- Every push to `main` starts the Build workflow without a filter of paths, because GitHub starts no run for a filtered push that changes too many files, and the formatting check installs the release of clang-format the workflow pins, the one the sources are formatted with.
- The Release workflow runs the same builds for a version tag and publishes every package on the release.
- Until the first stable version `main` holds one commit and the repository one release, each change is amended into that commit and pushed with force, the release is published again from its tag, and only the runs of that commit and that release are kept.
- The macOS package is a disk image whose bundle carries the runtime library in `Frameworks` and the resources in `Resources`, says in English and Portuguese why pages may ask for the camera and the microphone, and is signed after assembly with the configured identity or ad hoc, where an identity signs with the hardened runtime, a secure timestamp and the camera and audio input entitlements, the validation refuses a bundle missing any of them, and the workflows notarize the disk image whenever the secrets hold the identity.
- The Windows package is an NSIS installer laying down `bin` and `share/workpane`, with a desktop shortcut and the runtime of Visual C++ beside the executable, so a machine without its redistributable still starts the product.
- The Linux package is a Debian package under `/opt/workpane` with a desktop entry, an icon and a `workpane` command, whose library dependencies are read from what it links.
- The packages carry only the install component of the product, so no dependency installs anything into them.

## Documentation writing standard

- The README presents the project the way a project page does, with a short introduction, features that name only what the product does in a few words, without its languages, themes, games or installers, which screenshots may show, the download and links to `docs`, while every detail lives in `docs`.
- The development guide `docs/development.md` lists every command of `make.py`, and the audits refuse one that is missing.
- A sentence never begins with code, a path or a fenced block, so a line always opens with a word.
- Every sentence a person reads, in a message, an error, a log entry, a notification, command line output, a comment, a document or a commit, starts with a capital letter, and a sentence that would open with code, a command or anything else written in lowercase gets a word before it, as in `Use the command "xcrun devicectl list devices" to list them.`
- A command, an option, a path, an identifier or any other reserved expression inside a sentence is marked, with backticks in Markdown and comments and with double quotes in messages, logs and command output, so it never reads as part of the prose.
- Prose never divides sentences with semicolons and never repeats what the code already says.
- CLAUDE.md writes a number only when the number is the rule itself, such as the version of the language or the oldest version of a system the product supports, and never one that is secondary to a rule, such as the version of a library or of a stored schema, or a limit, a size, a count or a duration the code declares, which it names as the bound the rule keeps.
- The architecture, plugin and component references in `docs` are updated in the same change as the behavior they describe.
- The plan in `PLAN.md` holds the checklist, the review and the review of the review, and every item is checked only when it is built, tested and documented.
- A request too large for one sitting is written first as a detailed list in `MISSING.md`, checked item by item and emptied once every item is done.
- The plan and the list stay on the machine where the work happens, `.gitignore` keeps both out of the repository, and the prose audit covers them while they exist.
