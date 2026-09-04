# Packaging

```bash
python3 make.py package
python3 make.py validate-package
```

Every platform produces exactly one file, and that file is what the reader opens. macOS gets a disk
image, Windows gets an installer and Linux gets a Debian package.

An installer rather than an archive on Windows, because an archive downloaded through a browser
arrives inside a second archive and leaves the reader to decide where the application should live.

## Where a package puts the application

Windows installs under Program Files and creates a Start Menu entry and a desktop shortcut, both
pointing at the executable, and the uninstaller removes them.

Linux installs the whole application under `/opt/workpane`, because the package carries its own Qt
and does not share the libraries of the distribution. Beside it the package installs a desktop entry,
an application icon and the `workpane` command a shell already looks for.

## What a package contains

The application executable, all eight plugins, the shared Qt libraries, the Qt WebEngine helper
process and its resources, and the shared `hwinfo` components the System Information plugin links.

Every plugin is installed **beside the executable** rather than beside the package root, because that
is where the application looks for its own plugins and where the development build already puts them.
A package that puts them anywhere else opens nothing at all, and says nothing while it fails.

On Windows the shared `hwinfo` components also ship beside the executable, because a Windows loader
resolves a dependent library from there and a plugin that cannot load stops the whole start.

macOS assembly places the plugins and the components in the bundle before Qt deployment analyzes
runtime dependencies. Windows and Linux deployment analyze every plugin library in addition to the
executable, so a Qt module only a plugin uses is never omitted.

## What validation checks

`validate-package` reads the content the package really carries rather than asking whether a file
exists, and holds it to:

- the application executable in its `bin` directory
- the eight plugins beside that executable
- the Qt WebEngine helper process
- Qt shipped as a shared library
- the eight hardware components
- on macOS, the assembled bundle signature
- on Linux, the desktop entry, the icon and the `workpane` command linked into the search path

The Debian package is opened with `dpkg-deb`, which is the reader of that format. macOS and Windows
are held to the assembled tree their generator compressed, because a disk image and an installer are
not archives anything reads back, and that tree is what the reader ends up with either way.

## Signing

macOS signing and notarization run only when the repository provides the signing certificate, the
identity and the notarization credentials, and the build falls back to an ad-hoc signature for
unsigned validation builds. The hardened runtime and a secure timestamp are applied only when a real
identity is selected, and signing happens only after dependency paths and bundle contents are final.

## Versioning

The version lives only in the root `project()` declaration, reaches the application through the
generated build header and is shown in the application settings. The bundle identifier is declared
once beside it and reaches both the macOS bundle and the Windows manifest.

```bash
python3 make.py version
python3 make.py version 1.2.3
```

## Related

- [Development](development.md) — the tasks and the gates
