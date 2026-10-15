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
| `python3 make.py staple-package <dmg>` | Attaches and validates an accepted Apple ticket, with bounded retries for unavailable ticket delivery |
| `python3 make.py reset-data` | Removes the database and every plugin state |
| `python3 make.py run` | Builds and runs the application, with an optional data folder |
| `python3 make.py sanitize` | Runs the tests with the address and undefined behavior sanitizers |
| `python3 make.py test` | Builds and runs every test |
| `python3 make.py validate-package` | Checks what the installer carries |
| `python3 make.py version` | Prints the version, or sets a new one such as `python3 make.py version 0.0.2` |

Coverage configures its own Debug build and reads the compiler identity CMake generates. Clang data is read through `llvm-cov gcov`, using `xcrun llvm-cov gcov` on macOS. The coverage task requires `gcovr` and checks the line coverage floor declared in the task runner.

Builds are Debug by default, and `--configuration Release` selects a release build.

The POSIX SSH integration cases require the native OpenSSH commands `ssh`, `sshd` and `ssh-keygen`. They start their own loopback daemon with temporary keys in a private folder under the native account home and explicit configuration, retain native ownership checks, use native SDK programs for the connection owner and actual profile actions for authentication and disconnect and never enable or reconfigure the system's remote login service. Their HTTP fixture starts a real loopback listener without reverse DNS. The terminal case drives the actual component keyboard, interruption, dimensions, output selection, restart history and disconnect cleanup. An idle connection case waits beyond its connection lease while the product is hidden to prove renewal continues without drawing. Native browser editing tests require an unlocked desktop whose window manager can activate their window.

Linux test runners install `openssh-server` as well as the client. Windows integration requires `ssh`, `sshd`, `ssh-keygen` and `sftp-server`, supplied in CI by digest-pinned Win32 OpenSSH archives. Isolated debug daemons run under the current account, with a separate listener for each accepted channel. The native cases exercise the production session, SFTP file facade, protected permissions, binary streams, native job ownership and ConPTY without changing a system service. A domain account still needs its own authentication acceptance on a domain-joined host.

The AI stream fixture uses a real loopback HTTP server and advances its controlled activity clock after the first fragment arrives. Cancellation and stream inactivity each retain their actual socket closure assertions, independently of network startup scheduling.

Native browser cases use the actual WebKit, WebKitGTK or WebView2 input path and platform clipboard. JavaScript observes the edited value and focuses its input, while native events perform editing. The desktop resource lock serializes these cases with every other native window or clipboard case, including isolated Windows SSH daemons that open a console. Linux uses the product ImGui clipboard callbacks backed by GTK, so the case checks transfer between the product and native page without involving a second X11 clipboard owner on the same thread, including text copied before the first browser opens. Headless interface and product harnesses use recording clipboard callbacks of their own, so parallel suites never contend for the desktop clipboard.

Windows browser focus checks read the foreground GUI input queue and require the host in the foreground with its native child focused after a physical click. File inheritance checks compare actual access entries and protection against an ordinary file created in the same directory, including entry flags, access masks and trustee SIDs, rather than comparing automatic inheritance bookkeeping.

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

The [architecture](architecture.md) explains how the parts fit together. Build task regressions live in `tests/build-tools` and run through the Python standard unittest library, with each case registered separately in CTest alongside the native suites. The standard in [AGENTS.md](../AGENTS.md) holds the rules every change follows.

## Dependencies

CMake downloads every library on the first build, each pinned to one archive and its SHA-256: Dear ImGui, GLFW, FreeType, Varn, SQLite, webview on macOS and Linux, the WebView2 SDK on Windows, portable-file-dialogs, ImGuiColorTextEdit, libvterm, miniaudio, `nlohmann/json`, cpp-httplib, stb and GoogleTest.

The library hosting cases run in `WorkpaneLibraryTests`, which links the actual core without the desktop application's Windows process manifest. They check Unicode paths and resources, structured filesystem failures and encoded HTTP filenames. They also compare the linked OpenSSL version with the source version configured by the dependency graph, so a downloaded security update cannot pass while the build still links an earlier library.

Varn fetches CPM and each library it builds from an archive pinned by its digest, so the archive digest and its declared patches pin every source the runtime builds. The audits refuse any declaration or download of the product that carries no digest.

The runtime's explicit archive include paths precede compiler environment paths, so an installed libzip cannot replace the pinned headers. A build regression resolves the runtime object's output from the compilation directory declared in its compilation database entry, resolves that object's recorded Ninja dependencies from the build directory and checks both archive headers against their pinned source and generated configuration paths.

A few of them carry patches from `cmake/patches`:

- The code editor centers its rows, leaves finding to the find bar of Workpane and keeps its markers in place while text changes
- libvterm keeps faint text and OSC 8 hyperlinks
- GLFW hands the text an input method composes to Workpane on macOS and X11
- webview keeps the data of its pages under the data folder and registers no page handlers of its own on Windows
- portable-file-dialogs starts its helper programs through the system with no descriptor of Workpane and the signals of a shell

## Done means

The product version is declared by the CMake project and changed through the version task. The separate build number lives in `cmake/Version.cmake`. Application information exposes it as `buildNumber`, settings show both values and the macOS bundle uses the build number for `CFBundleVersion` while `CFBundleShortVersionString` keeps the product version. Package validation checks those declarations independently.

POSIX packages carry `WorkpaneTerminal` beside the application executable. This small session executable acquires the controlling pseudo-terminal after spawning in a new session and then replaces itself with the requested shell. Native terminal regressions exercise foreground interruption, background job survival and private input through `/dev/tty`.

A change is finished when:

- The build has no warning
- Every test passes
- The formatting check and the audits pass
- The references in `docs` and `AGENTS.md` describe the new behavior

## Continuous integration

The Build workflow runs on every push to `main`. It checks formatting and the audits once, then builds, tests, packages and validates Workpane on six targets: Linux, macOS and Windows, each on x86_64 and arm64.

The runners of macOS and Windows have no graphics device, so the one test that opens the OpenGL window of Workpane is skipped there and runs on Linux and on your machine.

The Release workflow runs the same builds for a tag such as `v0.0.1` and publishes the six installers on the release.

When the secrets of the repository hold a Developer ID identity, the macOS bundles are signed with it under the hardened runtime and the disk images are notarized. Without it they are signed ad hoc.

The macOS package signs its runtime libraries and native helpers before signing the enclosing application. Distribution signatures carry the hardened runtime and secure timestamp on every binary, and the application alone receives its camera and microphone entitlements. Package validation checks every nested signature before notarization.

## Releasing

Until the first stable version, `main` keeps one commit and the repository one release:

1. Set the version with `python3 make.py version 0.0.1` when it changes
2. Amend the single commit, whose message stays one short lowercase line opened by its kind, such as `feature: workpane`, and push it with force
3. Remove the previous release and its tag
4. Push the tag of the version, which starts the Release workflow

The product fixes the macOS deployment target to `13.3` before CMake initializes the toolchain, including when configuring an existing build folder. Package validation checks that the bundle, the executable, the terminal helper and every architecture of the runtime library agree on that minimum.

Native Windows browser acceptance runs through a CTest launcher that declares an owned temporary input desktop when creating the test process. Every browser thread and child shares it, while physical input, actual focus and the native clipboard remain the acceptance path. The launcher owns the descendants in a native job, stops them within its deadline and restores the previous input desktop after they close.

When the launcher is elevated, it starts the browser child with a restricted standard user token at medium integrity. Its private desktop grants access to that account and SYSTEM, carries the same integrity level and is named with its full window station, keeping the browser engine in the child's graphical session. The restricted token also declares a default object ACL for the same account and SYSTEM, preserving access to its process and thread objects after removing administrator groups.

The command `python3 make.py staple-package <dmg>` attaches and validates the ticket after Apple accepts notarization. Each operation retries only the unavailable ticket delivery status within its bounded attempts and delay. Invalid format, signature and other failures stop immediately, and exhausted delivery still fails the workflow. The native builds and Release retain both ticket operations as required gates.

## Foundation development

The option `WORKPANE_BUILD_APPLICATION=OFF` configures the source as a reusable library without the Workpane executable, bundled plugins or packaging. Subdirectory consumers select that mode by default. The [foundation guide](foundation.md) documents the public CMake targets, application factory, installation rules and example. The ordinary task runner commands continue to build and test the reference Workpane product.
