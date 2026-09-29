# Development

## Commands

Every task runs through one script at the root of the repository.

| Command | What it does |
| --- | --- |
| `python3 make.py all` | Checks formatting, runs the audits, builds and runs the tests |
| `python3 make.py audit` | Runs the audits this project declares for itself |
| `python3 make.py build` | Builds the application |
| `python3 make.py clean` | Cleans the selected build folder |
| `python3 make.py configure` | Configures the selected build folder |
| `python3 make.py coverage` | Writes the coverage report |
| `python3 make.py distclean` | Removes every build folder |
| `python3 make.py doctor` | Checks the required and optional tools |
| `python3 make.py format` | Formats every C++ source |
| `python3 make.py format-check` | Fails on any source the formatter would change |
| `python3 make.py icons` | Writes the table of Lucide icon names after the icon font is replaced |
| `python3 make.py lint` | Runs the audits and Cppcheck |
| `python3 make.py package` | Builds the installer of the current platform |
| `python3 make.py reset-data` | Removes the database and every plugin state |
| `python3 make.py run` | Builds and runs the application, with an optional data folder |
| `python3 make.py sanitize` | Runs the tests with the address and undefined behavior sanitizers |
| `python3 make.py test` | Builds and runs every test |
| `python3 make.py validate-package` | Checks what the installer carries |
| `python3 make.py version` | Prints the version, or sets a new one such as `python3 make.py version 0.0.2` |

Builds are Debug by default, and `--configuration Release` selects a release build.

## Sources

| Folder | Contents |
| --- | --- |
| `src/app` | The window, the frame loop and the product the tests drive without a window |
| `src/audio` | The sounds of plugins |
| `src/execution` | The main thread queue and the worker pool |
| `src/files` | Listing, walking and searching folders |
| `src/http` | The server of local folders |
| `src/json` | The strict reader every message goes through |
| `src/localization` | Languages and catalogs |
| `src/logging` | The centralized log |
| `src/persistence` | SQLite, preferences, plugin tables and configuration transfer |
| `src/platform` | What each system does its own way, one folder per system and `posix` for what macOS and Linux share |
| `src/process` | The programs plugins run |
| `src/scripting` | The Lua runtime and the host functions of the SDK |
| `src/ui` | Fonts, icons, the theme, the components, Markdown and the shell |
| `lua/workpane` | The SDK every plugin runs on |
| `plugins` | The plugins the product carries |
| `tests` | The GoogleTest suites |

The [architecture](architecture.md) explains how the parts fit together, and [CLAUDE.md](../CLAUDE.md) holds the rules every change follows.

## Dependencies

CMake downloads every library on the first build, each pinned to one archive and its SHA-256: Dear ImGui, GLFW, FreeType, Varn, SQLite, webview on macOS and Linux, the WebView2 SDK on Windows, portable-file-dialogs, ImGuiColorTextEdit, libvterm, miniaudio, `nlohmann/json`, cpp-httplib, stb and GoogleTest.

Varn fetches CPM and each library it builds from an archive pinned by its digest, so the digest of the archive of Varn pins every source the runtime builds. The audits refuse any declaration or download of the product that carries no digest.

A few of them carry patches from `cmake/patches`:

- The code editor centers its rows, leaves finding to the find bar of Workpane and keeps its markers in place while text changes
- libvterm keeps faint text and OSC 8 hyperlinks
- GLFW hands the text an input method composes to Workpane on macOS and X11
- webview keeps the data of its pages under the data folder and registers no page handlers of its own on Windows
- portable-file-dialogs starts its helper programs through the system with no descriptor of Workpane and the signals of a shell

## Done means

A change is finished when:

- The build has no warning
- Every test passes
- The formatting check and the audits pass
- The references in `docs` and `CLAUDE.md` describe the new behavior

## Continuous integration

The Build workflow runs on every push to `main`. It checks formatting and the audits once, then builds, tests, packages and validates Workpane on six targets: Linux, macOS and Windows, each on x86_64 and arm64.

The runners of macOS and Windows have no graphics device, so the one test that opens the OpenGL window of Workpane is skipped there and runs on Linux and on your machine.

The Release workflow runs the same builds for a tag such as `v0.0.1` and publishes the six installers on the release.

When the secrets of the repository hold a Developer ID identity, the macOS bundles are signed with it under the hardened runtime and the disk images are notarized. Without it they are signed ad hoc.

## Releasing

Until the first stable version, `main` keeps one commit and the repository one release:

1. Set the version with `python3 make.py version 0.0.1` when it changes
2. Amend the single commit, whose message stays one short lowercase line opened by its kind, such as `feature: workpane`, and push it with force
3. Remove the previous release and its tag
4. Push the tag of the version, which starts the Release workflow
