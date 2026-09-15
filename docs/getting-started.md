# Getting started

Workpane is built with CMake and Ninja, driven by the repository task runner in `make.py`. The runner
needs nothing but a Python 3 standard library.

## Requirements

| Component | Requirement |
| --- | --- |
| C++ toolchain | C++20 support |
| Qt | Version 6.11.2 or newer, shared build, with Concurrent, Core, Gui, Network, Sql, WebEngine and Widgets |
| CMake | Version 3.28 or newer |
| Ninja | Current stable release |
| Zig | Required to build the pinned Ghostty dependency |
| Python | Version 3, standard library only |

Static Qt builds are rejected during configuration, because every plugin is a shared library that
links the same Qt the application does.

The minimum is 6.11.2 rather than 6.11 because `QImage::toCGImage` crashes inside `CGImageCreate` for
an image carrying its own colour space, which a web page reaches on macOS by asking for a cursor of
its own, and the crash takes the whole application with it.

## Check the toolchain

```bash
python3 make.py doctor
```

It names the first tool it cannot find, so a missing Zig or Ninja is reported before anything is
configured, and it names the Qt the build will use. A machine carrying more than one Qt resolves the
first one on its search path, so the runner passes the installation it found unless the environment
already names one through `CMAKE_PREFIX_PATH`, `Qt6_DIR` or `QT_ROOT_DIR`.

## Build

```bash
python3 make.py configure --configuration Debug
python3 make.py build --configuration Debug
```

## Run

The run task builds the selected configuration first.

```bash
python3 make.py run --configuration Debug
```

## Where the data lives

Everything Workpane stores lives in one SQLite database in the platform local data directory, and a
run reads and writes nothing else.

Pass an absolute directory to work against isolated state instead:

```bash
python3 make.py run -- --data-dir /absolute/path
```

A directory that is not absolute, one that is empty and one that cannot be created are refused by
name rather than answered with the platform location.

## Next

- [Architecture](architecture.md) — what the core owns and what a plugin owns
- [Development](development.md) — the tasks the runner answers
- [Plugins](plugins.md) — the eight features and what each one holds
