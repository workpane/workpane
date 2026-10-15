# README and documentation

Your job is to write or rewrite the README and the `docs` folder of this project so a newcomer understands what it is in thirty seconds and can run it in five minutes, and a contributor finds every technical detail in one obvious place. Everything you write is true of the code as it is today.

## 1. Learn what the project really does

- Read the manifests, the entry points, the routes or commands, the configuration, the build and release scripts, the continuous integration workflows, the license file and the existing documents. Run the help of a command line tool, list the routes of a server and open the screens of an application when you can.
- Build a list of features from the code, each one traced to where it lives. A feature you cannot find in the code does not go in the README, even if an old document or an issue mentions it. Never write a feature that is planned, partial or behind a disabled flag as if it shipped.
- Find the real install and run paths: the package registry name, the release assets, the container image, the minimum versions of the runtime and the system requirements declared in the manifests.
- Find the audience: an end-user application, a library, a command line tool, a service, a framework. The README of each answers different first questions.
- Keep what is good in the existing documents. A rewrite preserves correct content, links others depend on and the anchors of headings that are linked from elsewhere.

## 2. Structure of the README

Follow the README structure of the documentation section, adapted to the project and including only the sections it really needs. A complete skeleton for a fictional command line tool looks like this:

````markdown
<p align="center">
  <a href="https://github.com/acme/tidewatch">
    <picture>
      <source media="(prefers-color-scheme: dark)" srcset="extras/images/logo-dark.png">
      <img src="extras/images/logo.png" alt="Tidewatch" width="160">
    </picture>
  </a>
</p>

<p align="center">
  <a href="https://github.com/acme/tidewatch/actions/workflows/build.yml"><img src="https://github.com/acme/tidewatch/actions/workflows/build.yml/badge.svg" alt="Build"></a>
  <a href="https://github.com/acme/tidewatch/releases"><img src="https://img.shields.io/github/v/release/acme/tidewatch" alt="Latest release"></a>
  <a href="https://crates.io/crates/tidewatch"><img src="https://img.shields.io/crates/v/tidewatch" alt="Crates.io version"></a>
  <a href="LICENSE"><img src="https://img.shields.io/github/license/acme/tidewatch" alt="License"></a>
  <img src="https://img.shields.io/badge/platforms-linux%20%7C%20macos%20%7C%20windows-blue" alt="Platforms">
</p>

<p align="center">Watch log files and alert your team when a pattern appears, from one small binary.</p>

## ✨ Features

- Follow many files and rotated logs
- Match plain text or regular expressions
- Send alerts to Slack, email or a webhook
- Silence repeated alerts for a time window

## 📸 Screenshots

<table align="center">
  <tr>
    <td align="center"><img src="extras/images/screenshot-watch.png" alt="Terminal showing three watched files and one alert" width="400"><br>Watching files</td>
    <td align="center"><img src="extras/images/screenshot-alert.png" alt="Slack message sent by an alert" width="400"><br>An alert in Slack</td>
  </tr>
</table>

## 📦 Installation

```sh
cargo install tidewatch
```

Prebuilt binaries for every platform are on the [releases page](https://github.com/acme/tidewatch/releases).

## ⚡ Quick start

```sh
tidewatch init
tidewatch watch /var/log/app.log --match "ERROR"
```

## 📚 Documentation

- [Getting started](docs/getting-started.md)
- [Configuration](docs/configuration.md)
- [Development](docs/development.md)
- [Architecture](docs/architecture.md)

## 🤝 Contributing

Read the [development guide](docs/development.md) to build and test the project, then open a pull request.

## 📄 License

Tidewatch is released under the [MIT License](LICENSE).

## 🙏 Credits

Built with [notify](https://crates.io/crates/notify) and [regex](https://crates.io/crates/regex).
````

Rules that the skeleton shows:

- The logo uses `<picture>` with a dark source only when a dark variant exists. Otherwise use a plain `<img>`. Keep the width modest, between 120 and 200 pixels, and link it to the project.
- The pitch is one sentence of what it is and for whom. No adjectives that the features do not prove.
- Feature items name what the product does in a few words, never how it is built, which languages or libraries it uses, or how many tests it has.
- Section titles start with one emoji that matches them, consistently across the README.
- Screenshots sit in a centered table with captions and descriptive alternative text. Produce or update them through the images section, or describe what the reader should capture when no tool can make them. Never leave a broken or placeholder image.
- The quick start is the shortest path that works on a clean machine, with exact commands in fenced blocks marked with their language.

## 3. Choose badges that are real

- Build status comes from the actual workflow file: `https://github.com/<owner>/<repo>/actions/workflows/<file>.yml/badge.svg`, linked to the workflow page. Check the file name in `.github/workflows` and that the workflow runs on the default branch. Use the badge of GitLab, CircleCI or another service only when the project uses it.
- The version comes from where users get it: `img.shields.io/npm/v/<package>`, `pypi/v/<package>`, `crates/v/<crate>`, `maven-central/v/<group>/<artifact>`, `packagist/v/<vendor>/<package>`, `gem/v/<gem>`, `pub/v/<package>`, `nuget/v/<package>`, or `github/v/release/<owner>/<repo>` for release assets. Use only a registry where the package is really published under that name.
- License from `img.shields.io/github/license/<owner>/<repo>` or a static badge that matches the `LICENSE` file exactly.
- Platforms, language or minimum runtime as a static badge only when the build really supports what it says.
- Leave out coverage, downloads or quality badges when the project does not publish those numbers. Every badge has alternative text.

## 4. The documents in `docs`

Write one document per subject, only those the project needs, each opening with one sentence that says what it covers:

- The file `getting-started.md`: requirements with minimum versions, installation by every supported channel, first run, and where to go next.
- The file `development.md`: cloning, installing dependencies, building, running, testing, coverage, linting, formatting, and the layout of the repository. Every command the project's scripts or task runner define appears with what it does.
- The file `architecture.md`: the main components, how a request or an action flows through them, where data is stored, the boundaries and the decisions that shaped them. A diagram helps when it shows the real flow, written in Mermaid when the host renders it.
- The file `configuration.md`: every option, environment variable and configuration file key, with its type, default, allowed values and an example, read from the code that parses them.
- The file `api.md` or a generated reference: endpoints or public functions with parameters, responses, errors and an example request. Prefer linking to the reference the project already generates, such as OpenAPI or docs built from documentation comments.
- The file `deployment.md`: how to build a release, the artifacts, environments, secrets by name never by value, migrations and the health checks.

Write plain, factual sentences in sentence case, never divide sentences with a semicolon, mark every command, path, option and identifier with backticks, and use fenced code blocks with a language for everything a reader types or copies. Explain each step once and link instead of repeating.

## 5. Verify everything

- Run every command in the README and the documents on this machine when it is safe and does not publish or change anything outside the working directory. When a command cannot run here, such as a store install or a deploy, check it against the scripts and say in the report that it was not run.
- Check every relative link and image path exists with the exact case, since links that work on a case-insensitive disk break on the host. Use relative links for files in the repository, so they work in forks and branches.
- Check every anchor link against the heading it points to, as the host generates anchors: lowercase, spaces turned into hyphens, punctuation removed, and emoji removed with the space after them leaving a leading hyphen. Avoid deep anchor links into headings that start with an emoji, or verify them in the rendered page.
- Check external links you add resolve, and badge URLs match the real owner, repository, workflow and package names.
- Render the Markdown when a previewer is available, and read the result on a narrow width.

## 6. Report

List the documents you created or rewrote, the features you documented with where each lives in the code, the commands you ran with their results, the commands you could not run, the content you removed because it was no longer true, and the images the reader still needs to provide.
