# C++ engineering

## Recognize the project

- Read the top-level `CMakeLists.txt` first: `cmake_minimum_required`, the `project()` languages and version, `CMAKE_CXX_STANDARD` or `target_compile_features(... cxx_std_20)`, and how targets, options and subdirectories are declared. Read `CMakePresets.json` for the configure, build and test presets CI uses.
- Find how dependencies arrive: `FetchContent` or CPM declarations, `vcpkg.json` with a baseline, `conanfile.py` or `conanfile.txt`, git submodules, or system packages through `find_package`.
- Read `.clang-format`, `.clang-tidy`, the warning and sanitizer modules under `cmake/`, and any task runner (`Makefile`, `justfile`, a Python script) that wraps the build. Use those commands rather than invoking the compiler yourself.
- Note the standard the project targets (C++17, 20 or 23), every compiler and standard library in its CI matrix (GCC with libstdc++, Clang with libc++, Apple Clang, MSVC), and the platforms it ships on. A feature is usable only when every one of them implements it.
- Read the project's own rules on exceptions, RTTI, naming, file layout and formatting. They vary more in C++ than in any other language and always win.

## Architecture

- Build the code as a set of CMake library targets with a thin executable on top: a core library holding the logic, platform libraries behind declared interfaces, and the executable that composes them. Tests link the same libraries the executable links.
- Keep the dependency direction one way: core code never includes platform, UI or third-party framework headers it does not need, and platform-specific code implements interfaces the core declares.
- Isolate platform code in folders per platform (`platform/linux`, `platform/macos`, `platform/windows`, shared POSIX code separately) selected by CMake, rather than `#ifdef` blocks spread through shared code.
- Prefer composition and value types over deep inheritance. Use virtual interfaces at real boundaries, templates and concepts for static polymorphism, and `std::variant` for closed sets of alternatives.
- Make ownership visible in every signature: a value or `std::unique_ptr` transfers ownership, a reference or `std::span` borrows, `std::shared_ptr` shares only when lifetime truly is shared.

## Project structure

```
project/
├── CMakeLists.txt           # Project, options, subdirectories
├── CMakePresets.json        # Configure, build and test presets for every platform
├── cmake/                   # Warnings, sanitizers, coverage, dependency declarations
├── src/
│   ├── core/                # Domain logic as a library target
│   ├── io/                  # Files, network, persistence
│   ├── platform/
│   │   ├── posix/           # Code shared by Linux and macOS
│   │   ├── linux/
│   │   ├── macos/
│   │   └── windows/
│   └── main.cpp             # Entry point only
├── tests/
│   ├── core/                # One suite per area, linking the same library
│   └── support/             # Shared fakes and fixtures
└── third_party/             # Vendored code only when it cannot be fetched
```

- One class per file pair when the project follows that rule, with the file named after the class, the header declaring it and the source defining its members.
- Everything lives in the project namespace, nested by area when the project does so. Never write `using namespace` in a header.
- Use `#pragma once` or include guards as the project does.
- Every file includes what it uses, in the group order the formatter enforces (own header first, then project, third-party and standard headers). Forward-declare in headers to cut compile-time dependencies when only pointers or references are needed.
- Document with `///` or `/** */` Doxygen comments only where the project writes them, keep comments rare, and never describe every member in a header.

## Patterns and practices

### Resources and ownership

- Follow RAII: every resource (memory, file, socket, lock, handle) is owned by an object whose destructor releases it. Wrap C handles in `std::unique_ptr` with a custom deleter or a small owning class.
- Follow the rule of zero: let members manage themselves so the class declares no destructor, copy or move operations. When one of them is needed, declare all five deliberately, and `= delete` copying for unique resources.
- Never use owning raw pointers, `new` or `delete` in application code. Use `std::make_unique` and `std::make_shared`.
- Pass cheap types by value, large read-only inputs by `const&`, sinks by value then `std::move`, and outputs as return values. Return values rely on copy elision, so never `return std::move(local)`.
- Mark everything `const` that can be: variables, member functions, references. Mark single-argument constructors `explicit`, results that must not be ignored `[[nodiscard]]`, and functions that cannot throw `noexcept`, especially move operations.

### Lifetimes

- The `std::string_view` and `std::span` never own. Never return them to a temporary or a local, never store them in a member unless the owner's lifetime is guaranteed, and never build one from a temporary `std::string`.
- References and iterators into containers are invalidated by insertion and erasure (`std::vector` reallocation in particular). Do not keep them across mutations.
- Lambdas captured by reference must not outlive their scope. Capture by value or by `shared_ptr` for callbacks that run later, and capture `this` only when the object outlives the callback.

### Errors

- Follow the project's error model. Some projects use exceptions for failures, others forbid them and return `std::expected<T, Error>` (C++23), a project `Result` type or error codes.
- Never let an exception cross a C boundary, a thread entry point or a destructor. Destructors are `noexcept`.
- Do not use assertions or `std::abort` for conditions the program can handle, such as bad input, missing files or failed calls. Validate closed sets of values explicitly and return a structured error for an unknown one.

### Concurrency

- Every thread has an owner that stops and joins it. Use `std::jthread` with `std::stop_token` only when every toolchain the project targets provides them, and otherwise a `std::thread` with an atomic stop flag or a condition variable, joined in the owner's destructor or shutdown.
- Protect shared mutable state with a `std::mutex` and `std::scoped_lock` (which also locks several mutexes without deadlock). Keep critical sections short and never call unknown code or wait on I/O while holding a lock.
- Wait on a `std::condition_variable` with a predicate, never a bare `wait`.
- Use `std::atomic` for independent flags and counters, with the default sequentially consistent ordering unless a measured need and a proof justify a weaker one.
- A data race is undefined behavior, even when it "only" reads. Every access to shared state is synchronized.

### Code shape

- Use early returns, keep nesting shallow, and separate validation, work and result with single blank lines, as the shared code-style rules describe.
- Many projects require `// clang-format off` and `// clang-format on` around lambdas that are formatted by hand, because the formatter breaks them badly. Follow that rule exactly where the project has it, and assign a complex lambda to a named local before passing it.
- Prefer algorithms and ranges (`std::ranges::find_if`, `std::views::filter`) where they read clearly, and range-based `for` otherwise.
- Use `enum class`, `constexpr` constants scoped to the class or namespace that owns them, and `std::optional` for values that may be absent. Avoid macros except for build configuration.

## Data, networking and persistence

- Parse external data with a maintained library (a JSON library, SQLite through its C API wrapped in RAII, an HTTP client) and validate every field, length and range before use.
- Keep blocking I/O off threads that must stay responsive, such as UI or event loop threads, and return results through a queue the owning thread drains.
- With SQLite, prepare statements, bind every value, finalize through RAII and run statements on the thread or connection the project designates.
- Treat file paths with `std::filesystem::path`, convert them to text explicitly with the encoding rules of each platform, and handle `std::filesystem_error` or the `error_code` overloads.

## Security

- Never index without a bounds check on data from outside. Prefer `std::span`, `at()` in non-hot paths, and checked conversions between integer types (`std::in_range`, explicit range checks before narrowing).
- Never use `strcpy`, `sprintf`, `gets` or unbounded `scanf`. Use `std::string`, `std::format` (where every toolchain supports it) or `snprintf` with sizes.
- Start child processes with an argument vector (`posix_spawn`, `CreateProcessW` with a quoted command line) and never through a shell string built from input.
- Zero and release secrets deliberately, and never log them.
- Enable hardening the project allows: `-D_FORTIFY_SOURCE`, `-fstack-protector-strong`, standard library hardening modes (`_GLIBCXX_ASSERTIONS`, `_LIBCPP_HARDENING_MODE`) and position independent executables.

## Undefined behavior traps

- Signed integer overflow, shifting by the width or more, and dividing `INT_MIN` by `-1`.
- Reading uninitialized variables. Initialize every variable at declaration and every member with a default member initializer.
- Dangling references, pointers and views, including `const&` bound to a member of a temporary and range adaptors over temporaries.
- Using a moved-from object other than assigning or destroying it, unless the type documents its state.
- Type punning through `reinterpret_cast` instead of `std::bit_cast` or `std::memcpy`, and casting away `const` to write.
- Modifying a container while iterating it, and erasing during range-based `for`.
- Calling a virtual function from a constructor or destructor expecting the derived override.

## Performance

- Measure with a profiler (`perf`, Instruments, VTune, Tracy) and benchmarks (Google Benchmark) on a release build before optimizing.
- Avoid copies: move into sinks, reserve vectors, pass views, and return by value. Check that hot types are cheap to move.
- Prefer contiguous containers (`std::vector`, flat maps) over node-based ones in hot paths.
- Keep allocation, locking and logging out of tight loops.
- Reduce build times with forward declarations, private implementation types and lean headers.

## Tests

- Use the framework the project has, usually GoogleTest or Catch2, registered with CTest through `gtest_discover_tests` or `catch_discover_tests` so each test runs as its own CTest entry.
- Name tests for the behavior (`TEST(OrderParser, RejectsNegativeQuantity)`). Use fixtures for shared setup, parameterized tests for tables of cases, and fakes in a shared support folder for platform services.
- Cover success, failure, boundary and lifecycle paths, including destruction while work is pending and shutdown with threads running.
- Run the suite with `ctest --preset <name> --output-on-failure` or `ctest --test-dir build --output-on-failure`, in parallel with `-j`.
- Run the tests under AddressSanitizer and UndefinedBehaviorSanitizer (`-fsanitize=address,undefined -fno-omit-frame-pointer`) and, in a separate build, ThreadSanitizer (`-fsanitize=thread`) for concurrent code. MSVC supports `/fsanitize=address`.
- Measure coverage with GCC's `--coverage` and `gcovr` (`gcovr --html-details coverage.html` or `--txt`), or with Clang's `-fprofile-instr-generate -fcoverage-mapping`, then `llvm-profdata merge` and `llvm-cov report` or `llvm-cov show`. Use the coverage target or task the project defines when it has one.

## Tooling and quality gates

- Treat warnings as errors on first-party targets: `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow` and `-Werror` on GCC and Clang, `/W4 /WX /permissive-` on MSVC, set per target or with `COMPILE_WARNING_AS_ERROR` (CMake 3.24). Never weaken them, and never apply them to third-party code.
- Format with `clang-format` at the version the project pins, since output differs between releases, and check with `clang-format --dry-run --Werror`.
- Run `clang-tidy` with the project's `.clang-tidy`, through `CMAKE_CXX_CLANG_TIDY` or a script over `compile_commands.json` (`CMAKE_EXPORT_COMPILE_COMMANDS=ON`). Run Cppcheck or other analyzers where the project uses them.
- Use only `target_*` commands (`target_compile_options`, `target_compile_definitions`, `target_include_directories`, `target_link_libraries`) with `PRIVATE`, `PUBLIC` or `INTERFACE` chosen deliberately. Never set global `CMAKE_CXX_FLAGS`, `add_definitions` or `include_directories` for project code.
- Add every new source file to its target explicitly, as the project lists them, rather than relying on globbing.

## Build, configuration and release

- Configure and build with presets (`cmake --preset release`, `cmake --build --preset release`) and Ninja where the project uses it.
- Pin every dependency: `FetchContent_Declare` with a `URL` and `URL_HASH SHA256=...` or a commit hash, a vcpkg baseline, or a Conan lock file. Apply patches through the project's mechanism, never by editing fetched sources.
- Keep the version in one place, usually `project(VERSION ...)`, and generate build information from it.
- Ship release builds with optimizations, debug symbols split or stripped as the project decides, and the runtime libraries the platform needs.
- Use `install()` rules and CPack or the project's packaging scripts, and validate the package by running the installed executable.

## Pitfalls

- Using a C++20 or C++23 library feature that one toolchain in the matrix lacks, such as parts of `<format>`, `<ranges>`, `std::jthread`, `std::expected` or `std::from_chars` for floating point in older libc++.
- Global flags in CMake that leak into dependencies and break their builds.
- The `std::shared_ptr` everywhere instead of clear ownership, and cycles of `shared_ptr` that never free.
- Detached threads and callbacks that fire after their owner was destroyed.
- Implicit narrowing conversions hidden by casts instead of being handled.
- The `auto` deducing a copy where a reference was intended, or a proxy type such as `std::vector<bool>::reference`.
- Static initialization order dependencies between translation units.
- Platform `#ifdef` blocks in shared code instead of a platform folder.
- Code that compiles on one compiler only because of a non-standard extension.

## Definition of done

- The project builds from a clean configure with every preset CI uses and no warning.
- New sources are listed in their target, and flags and definitions are set with `target_*` commands.
- Ownership is expressed with values, references and smart pointers, with no owning raw pointer or manual `delete`.
- No view, span, reference or iterator outlives what it refers to.
- Errors follow the project's model, and no assertion or abort answers a recoverable condition.
- Every thread is joined by its owner, and shared state is synchronized.
- Only features every toolchain in the matrix implements are used.
- The command `clang-format` reports no difference, with lambda markers where the project requires them.
- The command `clang-tidy` and the other configured analyzers report nothing new.
- The tests pass through CTest, and the sanitizer builds report no finding.
- Coverage of the changed code was read from the project's coverage tool.
- Dependencies stay pinned by hash or lock file.
