# Reusable application foundation

Workpane can be used as the native foundation of another desktop application. The CMake target `Workpane::Core` provides the window and frame loop, shell, every visual component, platform services, persistence, the Lua SDK and the embedded Varn runtime. Features remain Lua plugins supplied by the application.

An application created through `workpane_add_application` loads no bundled Workpane plugin unless its plugin folder is explicitly passed. Its name, version, build, organization and identifier belong to the application. Several executable targets can share the same compiled core while keeping separate resources, plugins and data.

## Build only the library

Use a fresh build directory to configure the foundation without the Workpane desktop product:

```sh
cmake -S . -B build/library -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DWORKPANE_BUILD_APPLICATION=OFF
cmake --build build/library --target WorkpaneCore --parallel
```

The library is static. Its transitive CMake dependencies include the shared Varn runtime, so link the target rather than copying the archive alone. The build does not assemble Workpane resources, install its bundled plugins or enable its CPack configuration.

The option `WORKPANE_BUILD_APPLICATION` defaults to enabled when Workpane is the top-level project and disabled when it is a subproject. The option `WORKPANE_BUILD_TESTS` follows that application option by default. The Workpane application suites require the desktop product because they exercise its bundled features.

The supported source integration is `add_subdirectory` or `FetchContent`. Keep the source checkout pinned to the revision used by your application. The core owns its pinned dependency graph and exposes the required C++20 compile features through its target.

The runtime uses explicit include paths for its pinned archive library ahead of compiler environment paths. An installed libzip therefore cannot supply different headers while the runtime links the pinned library.

## Create your application

Place a pinned Workpane checkout under your project, for example at `vendor/workpane`, and define the application in the parent CMake project:

```cmake
cmake_minimum_required(VERSION 3.29)

set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "Minimum macOS version")
project(MyStudio VERSION 1.2.0 LANGUAGES C CXX)

add_subdirectory(vendor/workpane)

workpane_add_application(MyStudio
    NAME "My studio"
    VERSION "${PROJECT_VERSION}"
    BUILD 7
    ORGANIZATION "Example"
    IDENTIFIER "org.example.my-studio"
    PLUGINS "${CMAKE_CURRENT_SOURCE_DIR}/plugins"
)

workpane_install_application(MyStudio)
```

The factory generates the executable entry point, links `Workpane::Core`, stages the SDK and fonts, supplies the native helpers and copies Varn into the platform runtime layout. It does not require a custom C++ launcher or changes to the SDK.

Application target names must remain unique when case is ignored. Their bundles, executables and resource directories stay distinct on case-insensitive filesystems as well as on Linux. A parent that sets `CMAKE_POSITION_INDEPENDENT_CODE` before adding Workpane keeps that setting for the core and its dependencies.

Build and run the application with your own target name:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target MyStudio --parallel
```

The executable is `build/bin/MyStudio` on Linux, `build/bin/MyStudio.exe` on Windows and `build/bin/MyStudio.app/Contents/MacOS/MyStudio` on macOS. Its `--version` output uses the application name and version. Its `--help` uses the actual executable name, and `--data-dir` accepts an absolute data directory for an isolated run.

The file structure of a minimal project is:

```text
my-studio/
  CMakeLists.txt
  vendor/workpane/
  plugins/
    studio/
      plugin.lua
      translations.lua
      view.lua
```

Each folder passed through `PLUGINS` contains plugin folders, rather than a single plugin's files. The SDK discovers the staged plugins through their normal manifest, translation and lifecycle contracts. Several supplied folders are merged into the application's plugin directory, and duplicate folder identifiers are rejected at configuration time.

Omit `PLUGINS` to create the bare shell. The core settings remain available, and every native visual component remains registered. The Terminal, Code Editor, Browser, AI and other Workpane feature plugins are absent. Your plugins can use their underlying `terminal`, `codeEditor`, `webView`, Markdown, canvas, table, tree and form components directly through the [component API](components.md).

## Application arguments

The application factory accepts these arguments:

| Argument | Meaning |
| --- | --- |
| Target | A unique portable CMake target and executable name. |
| `NAME` | The required display name, also used by the window, native menus and version output. |
| `VERSION` | The required application version in `MAJOR.MINOR.PATCH` form. |
| `IDENTIFIER` | The required stable application identifier, such as `org.example.my-studio`. |
| `BUILD` | A positive build number, defaulting to the first build. |
| `ORGANIZATION` | The organization exposed to plugins, defaulting to the display name. |
| `DATA_DIRECTORY` | A portable directory name under the platform data location, defaulting to the identifier. |
| `PLUGINS` | The folders containing only the plugins this application should ship. Relative folders are resolved against the caller's source directory. |
| `LOGO` | An optional application image staged as `images/logo.png`. |
| `ICON` | An optional macOS application icon staged in the bundle. |

The factory rejects unknown or incomplete arguments, malformed versions, unsafe identifiers and directory names, nonexistent resource folders and duplicate plugin identifiers. Names and organizations can contain Unicode and quoted text. Application identities are also validated by the C++ startup API before the database is opened.

The minimum supported macOS deployment target is selected before the parent `project` command. Workpane preserves a supported deployment target supplied by the parent rather than replacing it. The other compiler and platform prerequisites are the same ones listed in [getting started](getting-started.md).

## Identity and data isolation

The Lua application API reports the consumer identity:

```lua
workpane.app.name
workpane.app.organization
workpane.app.identifier
workpane.app.version
workpane.app.buildNumber
```

The engine identity is available separately as `workpane.app.engineVersion` and `workpane.app.engineBuildNumber`. The SDK level remains `workpane.app.sdk`.

Application data defaults to `Library/Application Support/<identifier>` on macOS, the local application data folder followed by the identifier on Windows and the XDG data directory followed by the identifier on Linux. Use a stable identifier across application upgrades and a different identifier for a different product. The reference Workpane desktop product explicitly retains its existing data folder.

Every native credential belongs to the application identifier and the plugin identifier. Two applications using the same plugin and credential name do not share a stored password. Preference documents, plugin tables, web data and instance locks live under the application's own data directory. The reader can still explicitly add plugin folders in the core settings of that application.

## Install and package

The function `workpane_install_application(MyStudio)` installs the application through its own component, named after the target. Select that component when installing:

```sh
cmake --install build --prefix install --component MyStudio
```

On macOS the installed bundle contains its SDK, fonts, selected plugins, native helpers and Varn in its own `Resources`, `MacOS` and `Frameworks` directories. The installer signs the runtime libraries and native helpers before signing the enclosing bundle with `WORKPANE_CODESIGN_IDENTITY`, which defaults to an ad hoc identity. A configured identity gives each native signature the hardened runtime and secure timestamp. Only the application receives the web view camera and audio entitlements.

On Windows the executable, Varn and compiler runtime are installed in `bin`, with the application resources under `share/<target-name-in-lowercase>`. Linux installs the executable and native helpers in `bin`, Varn in `lib` and resources under the same shared data layout. The Linux executable uses its relative runtime library path.

The factory does not configure another application's installer, desktop entry, package description or release process. The parent project owns its CPack configuration and chooses its application component. The Workpane desktop packages are configured only when `WORKPANE_BUILD_APPLICATION` is enabled.

Workpane is [MIT licensed](../LICENSE.md). Retain its copyright and license notice and the notices of the bundled dependencies and fonts when distributing a derived application.

## C++ hosting

The target `Workpane::Core` can also be linked into a C++ host that owns its entry point. The public startup type `workpane::app::ApplicationIdentity` carries the name, version, build, organization, identifier, resource directory name and data directory name. The method `Application::launch` handles the standard command line and starts the native application from that identity.

For a custom interface loop, `Product::open` accepts the identity, absolute application paths and the platform services. The host attaches a `ProductInterface`, starts the SDK and services the main thread queue and runtime through the product lifecycle. Its window and renderer must preserve the same interface thread, component update, event delivery and shutdown contracts described in [architecture](architecture.md).

A C++ host that creates its target directly owns staging and installing the SDK, fonts, selected plugins, Varn and native helpers in its declared layout. The application factory is the complete managed path for a normal standalone window.

Native filesystem paths remain native until a JSON or text boundary, where they use UTF-8. Bundled fonts use retained native mappings shared by their synthetic faces, and image loading accepts Unicode filenames without requiring a particular Windows process code page. The library hosting suites exercise that contract through a separate executable without the desktop application's Windows manifest.

Plugin folder preferences, plugin manifests and Lua source filenames also cross that boundary as UTF-8. The embedded Lua loader uses native wide file opens on Windows and preserves its source-file BOM and shebang handling. A host calling `Application::launch` supplies UTF-8 command-line arguments.

## Working example

The [example project](../examples/plugin-app) is a complete parent project. It consumes Workpane as a subdirectory and builds two applications against one core:

- The application `ExampleStudio` uses its own name, version, build and identifier and ships only its `studio` plugin.
- The application `PlainStudio` has another identity and ships no plugin.

Build both with:

```sh
cmake -S examples/plugin-app -B build/example -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/example --target ExampleStudio PlainStudio --parallel
cmake --install build/example --prefix install/example --component ExampleStudio
```

The sample plugin follows the same SDK as any other plugin. Its view uses the application's metadata and native components without reaching a host function or modifying C++. Continue with the [plugin guide](plugins.md) and [component reference](components.md) to build your features.
