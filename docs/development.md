# Development

Everything goes through the repository task runner, which needs nothing but a Python 3 standard
library.

## Tasks

| Task | Purpose |
| --- | --- |
| **all** | Check formatting, run the audits, build and run the registered test suites |
| **audit** | Run the audits this project declares for itself |
| **build** | Compile the selected configuration |
| **clean** | Clean the selected build directory |
| **configure** | Generate Ninja build files with CMake |
| **coverage** | Generate HTML and Cobertura reports with line and branch gates |
| **distclean** | Remove every generated build directory |
| **doctor** | Locate required and optional development tools |
| **format** | Format first-party C and C++ sources |
| **format-check** | Validate formatting without changing files |
| **lint** | Run the audits and then Cppcheck warning, performance and portability analysis |
| **models** | Rewrite the AI model catalog from a LiteLLM checkout |
| **package** | Create the native release package |
| **reset-data** | Remove the application database and every plugin state |
| **run** | Build and start the application |
| **sanitize** | Build with address and undefined-behaviour sanitizers |
| **test** | Build and run the registered CTest suites |
| **validate-package** | Open the produced package and verify what it must contain |
| **version** | Print the current version or write a new `MAJOR.MINOR.PATCH` value |

Every task takes `--configuration Debug`, `Release` or `RelWithDebInfo`.

## The gates

```bash
python3 make.py all
```

That checks the formatting, runs the audits, builds and runs the registered suites, because a rule the
aggregate skips is one nobody runs.

Warnings are errors for every first-party target, and the GCC and Clang set adds the two polymorphism
reports that neither `-Wall` nor `-Wextra` turns on.

## The audits

`make.py audit` runs the checks this project writes for itself, and needs no tool the platform has to
supply. They refuse, among other things:

- a lambda without its clang-format markers
- a comment that opens in lower case, ends without a period or sits apart from what it explains
- a translation key nothing reaches, and a catalog whose language was built from another one
- a call whose arguments do not match the sentence it formats
- a future continuation, a connection or a single shot timer that captures something without naming
  the object it reaches
- a dereferenced `sender()`
- a theme token no style sheet consumes, and one a style sheet writes that nothing substitutes
- a statement continued on the next line, and includes that fall out of their declared groups
- an anonymous namespace
- a suite size that disagrees with what the standard records

## Testing

Tests are organised by ownership into core, Browser, Code Editor, Donate, Logs, System Information, AI,
terminal and web-server suites, and CTest discovers every case independently.

Asynchronous waiting uses the shared `waitUntil` inside a GoogleTest assertion, so an expired condition
fails the test instead of ending it silently. Qt Test assertion and waiting macros are prohibited
because an expired `QTRY_` macro returns from the case and reports it as passed.

A thread a case starts records what it saw and the case asserts it after joining, because a GoogleTest
assertion is only thread safe where pthreads are.

```bash
python3 make.py test
python3 make.py sanitize
python3 make.py coverage
```

## Continuous integration

Every push to the default branch and every pull request builds Linux, macOS and Windows, runs the
registered suite, produces the platform package and validates it. The Linux workflow also runs the
audits. Formatting and Cppcheck stay local, because the runner ships versions far from the ones the
project is written against.

A tag matching `v*` builds every platform and publishes one release with the Linux archive, the macOS
disk image and the Windows archive.

## Related

- [Getting started](getting-started.md) — requirements and the first build
- [Packaging](packaging.md) — what a package must contain
