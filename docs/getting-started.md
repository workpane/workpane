# Getting started

## Download

Every release carries an installer for each system and processor.

| System | x86_64 | arm64 |
| --- | --- | --- |
| macOS | `workpane-macos-x86_64.dmg` | `workpane-macos-arm64.dmg` |
| Linux | `workpane-linux-x86_64.deb` | `workpane-linux-arm64.deb` |
| Windows | `workpane-windows-x86_64-installer.exe` | `workpane-windows-arm64-installer.exe` |

Get them from the [latest release](https://github.com/workpane/workpane/releases/latest).

## Requirements

To build Workpane yourself you need:

- A C++20 compiler: Clang 17, GCC 13 or MSVC 2022 and newer
- CMake 3.28 or newer, Ninja, Git and Python 3.12 or newer
- On Linux, the development files of X11, OpenGL, GTK 3 and WebKitGTK 4.1
- On Windows, the WebView2 runtime, which current Windows already carries

On Ubuntu:

```bash
sudo apt-get install -y git ninja-build pkg-config libgl1-mesa-dev libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxkbcommon-dev libgtk-3-dev libwebkit2gtk-4.1-dev
```

Every other library is downloaded and pinned by CMake on the first build.

## Build and run

```bash
python3 make.py build
python3 make.py run
```

To try something without touching your real data, give the run its own data folder:

```bash
python3 make.py run /tmp/workpane-data
```

The application also takes a few options of its own:

| Option | What it does |
| --- | --- |
| `--data-dir <path>` | Uses another data folder, given as an absolute path |
| `--version` | Prints the version |
| `--help` | Lists the options |

## Where your data lives

Workpane keeps everything in one SQLite database inside its data folder.

| System | Folder |
| --- | --- |
| macOS | `~/Library/Application Support/Workpane` |
| Linux | `~/.local/share/workpane`, or `workpane` inside the folder `XDG_DATA_HOME` names |
| Windows | `%LOCALAPPDATA%\Workpane` |

The Application settings export the whole configuration to one file and import it again, on this machine or another one.

## Plugins

Every feature is a plugin, and the Plugins settings list them with a switch to turn each one on or off.

| Plugin | What it does |
| --- | --- |
| AI | Runs tasks with your agents on a board of workspaces |
| Terminal | Runs your shells in tiled workspaces |
| Browser | Browses the web in tabs, with bookmarks |
| Code Editor | Edits the folders you open, with language servers |
| Web Server | Serves local folders and shows their requests |
| Logs | Keeps the log of the whole product |
| System Information | Shows the machine Workpane runs on |
| Donate | Opens the donation pages |
| Flappy Bird | Plays Flappy Bird, off until you turn it on |
| Task Hero | Runs a small adventure at the bottom of the window, off until you turn it on |
| Components | Shows every component in every state, off until you turn it on |

To load plugins of your own, add their folder in the Plugins settings and restart Workpane. The [plugin reference](plugins.md) shows how to write one.
