# Documentation

Update documentation only where the change makes it wrong or incomplete, in the place the project already documents that subject. Do not create documents nobody asked for.

## The README

The README is the front page of the project: a short, attractive summary of what it is and why it matters, with the technical details in the `docs` folder. When you create or rewrite a README, follow this structure, adapted to the project:

1. The logo centered at the top, linked to the project, inside `<p align="center">`, with a width that keeps it modest, and a dark mode variant through `<picture>` when the project has one.
2. A centered row of badges right below: build status, latest release or package version, license, main language or platform, and supported platforms. Use shields.io badges or the badges of the services the project uses, each with alternative text.
3. A centered one-line pitch that says what the project is and for whom.
4. Sections whose titles start with an emoji that matches them, such as `## 🚀 Project`, `## 💎 Why`, `## ✨ Features`, `## 📸 Screenshots`, `## 📥 Download` or `## 📦 Installation`, `## ⚡ Quick start`, `## 📚 Documentation`, `## 🤝 Contributing` and `## 📄 License`, including only those the project really needs.
5. Features named in a few words each, as a list, describing what the product does and never how it is built.
6. Screenshots in a centered table with captions when the product has an interface.
7. The shortest path to running it, with exact commands in fenced blocks.
8. Links to the documents in `docs` for every technical subject: getting started, development, architecture, configuration, API reference and deployment.
9. The license and the credits at the end.

Every detail beyond that summary lives in `docs`, one document per subject, each opening with what it covers and written in plain sentences with commands and paths marked as code.

## Keeping it true

- Commands, paths, options, environment variables and examples in the documentation must work exactly as written. Run them when you can.
- When you change behavior that is documented, update the document in the same change.
