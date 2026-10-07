<p align="center">
    <a href="https://github.com/workpane/workpane" target="_blank" rel="noopener noreferrer">
        <img width="176" src="extras/images/logo.png" alt="Workpane">
    </a>
</p>

<p align="center">
  <a href="https://github.com/workpane/workpane/actions/workflows/build.yml"><img src="https://github.com/workpane/workpane/actions/workflows/build.yml/badge.svg" alt="Workpane - Build"></a>
  <a href="https://github.com/workpane/workpane/blob/main/LICENSE.md"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="License: MIT"></a>
  <a href="https://isocpp.org"><img src="https://img.shields.io/badge/C%2B%2B-20-00599C.svg" alt="C++ 20"></a>
  <a href="https://github.com/varn-org/varn"><img src="https://img.shields.io/badge/Lua-Varn-000080.svg" alt="Lua through Varn"></a>
  <img src="https://img.shields.io/badge/platform-macOS%20%7C%20Linux%20%7C%20Windows-555555.svg" alt="Supported platforms">
</p>

<p align="center">
The native workspace for developers: terminals, code, browser, local servers and AI agents in one fast window.
</p>

<br>

## 🚀 Project

Building software today means juggling a terminal, an editor, a browser, a local server and, more and more, AI agents. Each one lives in its own window, and many are heavy web apps of their own.

Workpane brings them together in one native window, so the whole context of your work stays in one place. It is written in C++, drawn on the GPU, and every feature is a Lua plugin that you can turn on and off or write yourself.

## 💎 Why Workpane

- **Fast and light**: a native application that redraws only when something changes, so an idle window costs almost nothing.
- **Built to keep running**: every feature is an isolated plugin, and one that fails is stopped and reported while the rest keeps working.
- **Your work comes back**: tabs, layouts and settings are saved as you go, and a session that ended abruptly is restored on the next start.
- **Your data stays yours**: everything lives in one local database that you can export and import, and AI runs on the providers you choose, local models included.
- **Open to extension**: your own plugins use the same Lua API as the ones that ship with Workpane.
- **Proven on every platform**: every change passes hundreds of automated tests on macOS, Linux and Windows, on x86_64 and arm64.

## ✨ Features

- [x] Terminal
- [x] Code editor
- [x] Web browser
- [x] AI agents
- [x] Web server
- [x] System information
- [x] Logs
- [x] Plugins in Lua

## 📸 Screenshots

| | |
| :---: | :---: |
| <img src="extras/images/ss/01.png" alt="Terminal"> | <img src="extras/images/ss/02.png" alt="Code editor"> |
| **Terminal** | **Code editor** |
| <img src="extras/images/ss/03.png" alt="Web browser"> | <img src="extras/images/ss/04.png" alt="AI agents"> |
| **Web browser** | **AI agents** |
| <img src="extras/images/ss/05.png" alt="Web server"> | <img src="extras/images/ss/06.png" alt="System information"> |
| **Web server** | **System information** |
| <img src="extras/images/ss/07.png" alt="Settings"> | <img src="extras/images/ss/08.png" alt="Games"> |
| **Settings** | **Flappy Bird** and **Task Hero** |

## 📥 Download

Get the installer for your system from the [latest release](https://github.com/workpane/workpane/releases/latest).

## 📚 Documentation

- [Getting started](docs/getting-started.md) — download, build, run and where your data lives
- [Development](docs/development.md) — the commands, the sources, the checks and releases
- [Architecture](docs/architecture.md) — how the C++ and Lua halves fit together
- [Plugins](docs/plugins.md) — how to write a plugin
- [Components](docs/components.md) — every component a plugin can use

## ☕ Buy me a coffee

Support the continuous development of this project.

<a href='https://ko-fi.com/A0A412XEV' target='_blank'><img height='36' style='border:0px;height:36px;' src='https://storage.ko-fi.com/cdn/kofi2.png?v=6' border='0' alt='Buy Me a Coffee at ko-fi.com' /></a>

## 💛 Made with love

Made with ❤️ by [Paulo Coutinho](https://github.com/paulocoutinhox) and every person who opens an issue, sends a patch or tells us what broke.

## 📄 License

[MIT](LICENSE.md)

The bundled fonts, icons, sprites and sounds keep their own licenses, listed beside them: Inter and JetBrains Mono under the SIL Open Font License, Lucide under the ISC License, the Flappy Bird assets of Samuel Custodio under the MIT License and the Tiny Swords pack by Pixel Frog.

Copyright (c) 2026, Paulo Coutinho
