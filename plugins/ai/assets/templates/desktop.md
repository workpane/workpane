# Desktop application engineering

## Recognize the project

- Electron: `electron` in the dev dependencies of `package.json`, a `main` entry, and a packager such as Electron Forge (`forge.config.*`), electron-builder (`electron-builder.yml` or a `build` key) or electron-vite (`electron.vite.config.*`). Find the main, preload and renderer entry points before anything else.
- Tauri: a `src-tauri` folder with `Cargo.toml`, `tauri.conf.json` (or `.json5`, `.toml`), `capabilities/*.json` and `src/lib.rs`, beside a web frontend. The `tauri` crate version and the `@tauri-apps/api` version tell whether it is Tauri 2, which this section assumes. Tauri 1 used an `allowlist` instead of capabilities.
- Native: a macOS SwiftUI or AppKit app has an `.xcodeproj` with a macOS target, a WinUI or .NET app has `*.sln` and `*.csproj` with `UseWinUI` or `UseWPF`, and a Qt app has `CMakeLists.txt` with `find_package(Qt6 ...)` or a `.pro` file. Apply the platform rules of that language and the notes below.
- Read the commands in the scripts and CI: `npm run dev`, `npm run make` or `npm run dist`, `npm run tauri dev`, `npm run tauri build` (or `cargo tauri build`), `cargo test`, `dotnet build`, `cmake --build`. Use the package manager of the lock file.
- Note every operating system the app ships to. Each change must hold on all of them.

## Architecture

- A desktop app has a privileged core and an unprivileged interface. In Electron the main process is privileged and each renderer is a web page. In Tauri the Rust core is privileged and the webview is a web page. Treat the interface as untrusted: it may render remote content, a dependency may be compromised, and any script running there must not gain file system or shell access by default.
- The core owns windows, menus, the tray, the file system, processes, the network with credentials, storage and updates. The interface owns presentation and asks the core for everything else through a narrow, typed, validated API.
- Design that API as a list of named operations with typed arguments and results, such as `documents.open(path)` or `settings.update(patch)`, never a generic channel that executes whatever the interface sends.
- Structure the frontend as a web app of its framework, applying the rules of that framework. Structure the core by responsibility: windows, menus, IPC handlers, services, storage and updates.

## Project structure

```
src/
  main/                       # Electron main process: app lifecycle, windows, menus, IPC handlers
    ipc/                      # One handler module per domain, each validating its input
    services/                 # File system, storage, updates, OS integration
  preload/                    # contextBridge API exposed to renderers, typed
  renderer/                   # The web app: views, components, state
  shared/                     # Types and schemas of the IPC contract used by both sides
src-tauri/                    # Tauri instead of main and preload
  src/
    lib.rs                    # Builder, plugins, setup, generate_handler
    commands/                 # One module per domain of commands
    state.rs                  # Managed state
  capabilities/               # Permission sets per window
  tauri.conf.json             # Windows, bundle, security, updater
build/ or resources/          # Icons, entitlements, installer assets
```

- A new privileged operation goes into the core with its handler, its schema in `shared` and a typed wrapper in the preload or the frontend client. The interface never imports Node or Rust modules directly.

## Patterns and practices

### Electron process model

- Keep the secure defaults explicit in every `BrowserWindow`: `contextIsolation: true`, `sandbox: true`, `nodeIntegration: false`, `webSecurity: true`, and never enable `nodeIntegrationInWorker`, `nodeIntegrationInSubFrames` or `allowRunningInsecureContent`.
- Expose only specific functions from the preload with `contextBridge.exposeInMainWorld`, each calling one IPC channel. Never expose `ipcRenderer` itself, `send` with a channel argument, or Node modules.
- Use `ipcRenderer.invoke` with `ipcMain.handle` for requests and replies. Validate every argument in the main process with a schema, check `event.senderFrame` against the expected origin, and return plain serializable data.
- Load the app from a custom protocol registered with `protocol.handle` or from the packaged files, not from an arbitrary remote URL. When remote content is needed, load it in a separate sandboxed view without the preload.
- Block unexpected navigation and new windows with the `will-navigate` event and `webContents.setWindowOpenHandler`, and open external links with `shell.openExternal` only after allowing `https:` and `mailto:` URLs.
- Deny permission requests by default with `session.setPermissionRequestHandler` and `setPermissionCheckHandler`, allowing only what the app needs.
- Set a strict Content Security Policy through a `<meta>` tag or response headers, without `unsafe-eval` and with `unsafe-inline` avoided for scripts.
- Flip Electron fuses at packaging with `@electron/fuses`: disable `RunAsNode`, `EnableNodeOptionsEnvironmentVariable` and `EnableNodeCliInspectArguments`, and enable `EnableEmbeddedAsarIntegrityValidation` and `OnlyLoadAppFromAsar`.
- Keep Electron current within the supported major lines, since each release carries Chromium security fixes.

### Tauri 2

- Expose Rust functions with `#[tauri::command]`, register them in `tauri::generate_handler!`, and call them with `invoke` from `@tauri-apps/api/core`. Make commands `async` when they do I/O so they do not block the main thread, and return `Result<T, E>` where `E` serializes to a structured error.
- Grant permissions through capability files that name the windows they apply to and list exactly the permissions needed, such as specific `fs` scopes instead of the whole file system. Define permissions for your own commands, and never grant a broad default to a window that shows remote content.
- Keep shared state in `app.manage(...)` and read it with `tauri::State`, protected by a `Mutex` or `RwLock`, and do not hold a lock across an `.await`.
- Use the official plugins (`fs`, `dialog`, `shell`, `store`, `updater`, `window-state`, `global-shortcut`) with scoped permissions instead of writing commands that duplicate them.
- Set `app.security.csp` in `tauri.conf.json`. Consider the isolation pattern when the frontend has many dependencies.
- Use events (`emit` and `listen`) for notifications from core to interface and channels for streams, and commands for requests.

### Native desktop notes

- SwiftUI on macOS: use `WindowGroup`, `Window` or `DocumentGroup` scenes, a `Settings` scene for preferences opened with Command and comma, `.commands` with `CommandGroup` and `CommandMenu` for the menu bar, and `@SceneStorage` and `@AppStorage` for state. Follow the iOS section for Swift concurrency and testing. Enable the App Sandbox with only the entitlements needed, and keep access to user-chosen files across launches with security-scoped bookmarks.
- WinUI and .NET: use MVVM with CommunityToolkit.Mvvm (`ObservableObject`, `[ObservableProperty]`, `[RelayCommand]`), `async` commands that never block the dispatcher, dependency injection through `Microsoft.Extensions.Hosting`, and resources (`.resw`) for strings. Pickers in WinUI 3 need the window handle through `WinRT.Interop.InitializeWithWindow`. Know whether the app is packaged (MSIX identity) or unpackaged, since storage and APIs differ.
- Qt: choose Qt Quick (QML) for fluid modern interfaces and Qt Widgets for dense traditional tools, as the project already does. Keep logic in C++ classes exposed to QML through `QML_ELEMENT` and properties with `NOTIFY` signals, never in long QML JavaScript. Use `QSettings`, `QStandardPaths` and `QKeySequence::StandardKey`.

### Behavior every desktop app owes

- Window state: save and restore size, position and maximized state per window, and clamp a restored window onto a connected display, since monitors change. Use `tauri-plugin-window-state`, a small store in Electron, `saveGeometry` and `restoreGeometry` in Qt, or scene restoration on macOS.
- Single instance: when a second launch should focus the first, use `app.requestSingleInstanceLock` in Electron or `tauri-plugin-single-instance`, and forward the arguments and opened files.
- Menus: on macOS provide the application menu with About, Settings, Hide and Quit, plus Edit with the standard roles so copy, paste and undo work in text fields. On Windows and Linux put Settings and Exit where those platforms expect them, or follow the in-window menu the design uses.
- Shortcuts: use Command on macOS and Control on Windows and Linux (`CmdOrCtrl` accelerators in Electron and Tauri), follow platform standards such as Command and comma for settings, Command and Q to quit on macOS and Control and Q on Linux, and never override system shortcuts.
- Files: use the native open and save dialogs, remember the last folder, write files atomically through a temporary file and rename, and handle files opened from the system (`open-file` on macOS, arguments on Windows and Linux).
- Paths: store data in the per-user locations each OS defines (`app.getPath('userData')`, the path resolver of Tauri, `QStandardPaths`), never beside the executable.
- Quit cleanly: ask before discarding unsaved work, flush storage and stop child processes on quit.

### Documentation comments

- Use the convention of each language: TSDoc in TypeScript, `///` in Rust and Swift, XML documentation comments in C#, Doxygen in C++, only on public APIs where the project already writes them. Comments stay rare.

## Data, networking and persistence

- Keep settings in a typed store with defaults and schema validation, such as `electron-store`, `tauri-plugin-store` or the settings API of the native platform, and migrate it when its shape changes.
- Use SQLite through the core (`better-sqlite3` in the Electron main process, `rusqlite` or `sqlx` in Tauri) for structured data, with migrations and a single writer. Never open the database from the renderer.
- Make network requests that carry credentials from the core, so tokens never reach the page, and pass the interface only the data it shows.
- Run long work, such as indexing or large file reads, off the main thread: worker threads or utility processes in Electron, async tasks or threads in Rust, background tasks in native apps.

## Interface

- Match the platform: native title bar behavior or a custom one with correct drag regions and window controls on each OS, system fonts or the fonts of the design system, and the accent color and dark mode of the system through `nativeTheme` in Electron or the theme events of Tauri.
- Support keyboard operation everywhere, with a visible focus, menus that show their accelerators and context menus on secondary click.
- Handle high density displays and per-monitor scaling, windows from very small to very large, and resizing without layout jumps.
- Use the accessibility of the web for webview interfaces and the native accessibility APIs for native apps (NSAccessibility through SwiftUI modifiers, UI Automation through `AutomationProperties` in WinUI, `QAccessible` and accessible names in Qt).

## Security

- Treat every IPC message and command call as untrusted input: schema validation, path checks against allowed roots after resolving links, and no shell strings. Use `execFile` or `spawn` with an argument array in Node and `std::process::Command` in Rust.
- Never use `eval`, `new Function` or `innerHTML` with remote or file content in the interface.
- Store credentials in the OS credential store: `safeStorage` in Electron for encrypting values at rest, the `keyring` crate in Tauri, the Keychain on macOS, the Credential Manager or DPAPI on Windows and the Secret Service on Linux.
- Register custom URL schemes deliberately and validate every deep link as untrusted.
- Sign every update and verify it before installing. Tauri updates are verified with the public key in `tauri.conf.json` and signed with `TAURI_SIGNING_PRIVATE_KEY` in CI. Electron updates come over HTTPS from the release feed of the signed app.
- Sign and notarize binaries so the OS trusts them, and never disable signature checks to make a build pass.

## Performance

- Measure startup: create the first window early, show it when ready with `ready-to-show` or the equivalent to avoid a white flash, and defer loading modules and services until needed.
- Keep the Electron main process and the Tauri main thread free of blocking work, since a blocked main process freezes every window.
- Limit the number of renderers and hidden windows, release them when closed, and watch memory per process.
- Virtualize long lists and avoid layout thrashing in the interface, as on the web.
- Batch and debounce IPC traffic for high frequency events such as progress or file watching.

## Tests

- Test the core logic with the unit test runner of its language: Vitest or Jest for Electron main and preload modules, `cargo test` for Tauri commands written as plain functions that take their dependencies, xUnit, NUnit or MSTest for .NET, Qt Test or GoogleTest for Qt, and Swift Testing or XCTest for macOS.
- Test IPC handlers through their validation: valid input succeeds, invalid input is refused with a structured error, and a path outside the allowed root is refused.
- Test the interface with the component testing tools of its framework.
- Run end-to-end tests with Playwright for Electron (`_electron.launch`) or WebdriverIO with `tauri-driver` for Tauri on the platforms it supports, covering the critical flows on each shipped OS in CI.
- Run coverage with the tool of each side, such as `vitest run --coverage`, `cargo llvm-cov` and `dotnet test --collect:"XPlat Code Coverage"`, and read the reports.

## Tooling and quality gates

- Frontend: the type checker, linter and formatter of the project (`tsc --noEmit`, ESLint, Prettier), with Electron-specific lint rules when configured.
- Rust: `cargo fmt --check`, `cargo clippy --all-targets -- -D warnings` and `cargo test` inside `src-tauri`.
- .NET: `dotnet format --verify-no-changes`, analyzers and `TreatWarningsAsErrors` as the project sets them. Qt and C++: the formatter, clang-tidy and compiler warnings the project enables.
- Audit dependencies with the package manager (`npm audit`, `cargo audit`) where the project does, since both the Node and Rust trees ship to users.

## Build, configuration and release

- Build on each target OS, usually in a CI matrix, since cross-building signed desktop packages is unreliable.
- macOS: sign with a Developer ID certificate with the hardened runtime and minimal entitlements, notarize with `notarytool` (through `@electron/notarize`, Tauri's built-in support or `xcrun notarytool submit --wait`), staple the ticket, and ship a `dmg`, a `pkg` or the App Store build. Universal or per-architecture builds must match what the project distributes.
- Windows: sign the executable and installer with a code signing certificate or the cloud signing service the project uses, and ship an `msi` (WiX), an NSIS `exe` or an `msix`. MSIX gives package identity and clean uninstall but changes file system and registry behavior.
- Linux: ship `AppImage`, `deb`, `rpm` or a Flatpak as the project does. Flatpak runs sandboxed, so file access goes through portals, and the desktop entry, icon and MIME types must be declared.
- Auto-update with the mechanism of the stack: Electron `autoUpdater` with Squirrel on Windows and macOS, `electron-updater` with electron-builder, or `tauri-plugin-updater` with a signed manifest. Updates need signed builds and a release channel strategy.
- Keep the version in one place (`package.json`, `tauri.conf.json` reading it, or the project file) and generate release notes from it. Keep certificates, passwords, notarization credentials and update keys in CI secrets, never in the repository.

## Pitfalls

- Turning on `nodeIntegration` or turning off `contextIsolation` or the sandbox to make a library work.
- Exposing a generic `ipcRenderer.send` or `invoke(channel, ...args)` through the preload, which gives any page script the whole core.
- Granting a Tauri window broad file system or shell permissions instead of scoped ones.
- Opening links from content with `shell.openExternal` without checking the scheme.
- Blocking the main process with synchronous file or network calls.
- Writing user data next to the executable or into the app bundle.
- Forgetting the macOS application and Edit menus, so copy and paste stop working.
- Hard-coding Control in shortcuts and labels on macOS.
- Shipping unsigned or unnotarized builds, or an updater that does not verify signatures.
- Testing on one OS and assuming paths, line endings, case sensitivity and file locking behave the same elsewhere.

## Definition of done

- The interface runs without Node access, with context isolation and sandbox in Electron and scoped capabilities in Tauri.
- Every new IPC channel or command has a typed contract, validates its input in the core and has tests for valid and refused input.
- A strict Content Security Policy applies, navigation and new windows are controlled, and external links are checked.
- Credentials live in the OS credential store and never reach the interface.
- Window state, menus and shortcuts follow each target OS, including the macOS application and Edit menus.
- File operations use native dialogs, per-user data locations and atomic writes.
- The linters, type checkers, formatters and `cargo clippy` with warnings as errors pass on every side.
- Unit tests pass, end-to-end tests pass where the project has them, and coverage reports were read.
- The packaged app builds, is signed and, on macOS, notarized for every OS the change affects.
- Updates are signed and verified, and the version is set in one place.
